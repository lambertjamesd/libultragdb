
#include <ultra64.h>
#include <string.h>
#include "serial.h"

static OSMesgQueue* __gdbDmaMessageQ;

#define KSEG0           0x80000000
#define KSEG1           0xA0000000

#define REG_BASE        0x1F800000
#define REG_ADDR(reg)   (KSEG1 | REG_BASE | (reg))

#define USB_LE_CFG      0x8000
#define USB_LE_CTR      0x4000

#define USB_CFG_ACT     0x0200
#define USB_CFG_RD      0x0400
#define USB_CFG_WR      0x0000

#define USB_STA_ACT     0x0200
#define USB_STA_RXF     0x0400
#define USB_STA_TXE     0x0800
#define USB_STA_PWR     0x1000
#define USB_STA_BSY     0x2000

#define USB_CMD_RD_NOP  (USB_LE_CFG | USB_LE_CTR | USB_CFG_RD)
#define USB_CMD_RD      (USB_LE_CFG | USB_LE_CTR | USB_CFG_RD | USB_CFG_ACT)
#define USB_CMD_WR_NOP  (USB_LE_CFG | USB_LE_CTR | USB_CFG_WR)
#define USB_CMD_WR      (USB_LE_CFG | USB_LE_CTR | USB_CFG_WR | USB_CFG_ACT)

#define HEADER_TEXT_LENGTH      4
#define MESSAGE_HEADER_SIZE     8
#define MESSAGE_FOOTER_SIZE     4

#define ALIGN_16_BYTES(input)   (((input) + 0xF) & ~0xF)
#define ALIGN_2_BYTES(input)   (((input) + 0x1) & ~0x1)

#define USB_MIN_SIZE            16

extern void println(char* text);

// used to ensure that the memory buffers are aligned to 8 bytes
long long __gdbUnusedAlign;
char gdbSerialSendBuffer[GDB_USB_SERIAL_SIZE];
char gdbSerialReadBuffer[GDB_USB_SERIAL_SIZE];
static OSPiHandle gdbSerialHandle;
static OSMesgQueue gdbSerialSemaphore;
static OSMesg gdbSerialSemaphoreMsg;

static char gdbHeaderText[] = "DMA@";
static char gdbFooterText[] = "CMPH";

enum GDBEVRegister {
    GDB_EV_REGISTER_USB_CFG = 0x0004,
    GDB_EV_REGISTER_USB_TIMER = 0x000C,
    GDB_EV_REGISTER_USB_DATA = 0x0400,
    GDB_EV_REGISTER_SYS_CFG = 0x8000,
    GDB_EV_REGISTER_KEY = 0x8004,
};

enum GDBError gdbDMARead(void* ram, u32 piAddress, u32 len) {
	OSIoMesg dmaIoMesgBuf;

    dmaIoMesgBuf.hdr.pri = OS_MESG_PRI_NORMAL;
    dmaIoMesgBuf.hdr.retQueue = __gdbDmaMessageQ;
    dmaIoMesgBuf.dramAddr = ram;
    dmaIoMesgBuf.devAddr = piAddress & 0x1FFFFFFF;
    dmaIoMesgBuf.size = len;

    osInvalDCache(ram, len);
    if (osEPiStartDma(&gdbSerialHandle, &dmaIoMesgBuf, OS_READ) == -1)
    {
        return GDBErrorDMA;
    }

    osRecvMesg(__gdbDmaMessageQ, NULL, OS_MESG_BLOCK);

    return GDBErrorNone;
}

enum GDBError gdbDMAWrite(void* ram, u32 piAddress, u32 len) {
	OSIoMesg dmaIoMesgBuf;

    dmaIoMesgBuf.hdr.pri = OS_MESG_PRI_NORMAL;
    dmaIoMesgBuf.hdr.retQueue = __gdbDmaMessageQ;
    dmaIoMesgBuf.dramAddr = ram;
    dmaIoMesgBuf.devAddr = piAddress & 0x1FFFFFFF;
    dmaIoMesgBuf.size = len;

    osWritebackDCache(ram, len);
    if (osEPiStartDma(&gdbSerialHandle, &dmaIoMesgBuf, OS_WRITE) == -1)
    {
        return GDBErrorDMA;
    }

    osRecvMesg(__gdbDmaMessageQ, NULL, OS_MESG_BLOCK);

    return GDBErrorNone;
}

u32 gdbReadReg(enum GDBEVRegister reg) {
    return *((u32*)REG_ADDR(reg));
}

void gdbWriteReg(enum GDBEVRegister reg, u32 value) {
    *((u32*)REG_ADDR(reg)) = value;
}

enum GDBError gdbUsbBusy() {
    u32 tout = 0;
    enum GDBError err;
    u32 registerValue;

    do {
        if (tout++ > 8192) {
            gdbWriteReg(GDB_EV_REGISTER_USB_CFG, USB_CMD_RD_NOP);
            return GDBErrorUSBTimeout;
        }
        registerValue = gdbReadReg(GDB_EV_REGISTER_USB_CFG);
    } while ((registerValue & USB_STA_ACT) != 0);

    return GDBErrorNone;
}

u8 gdbSerialCanRead() {
    return (gdbReadReg(GDB_EV_REGISTER_USB_CFG) & (USB_STA_PWR | USB_STA_RXF)) == USB_STA_PWR;
}


u8 gdbSerialCanWrite() {
    return (gdbReadReg(GDB_EV_REGISTER_USB_CFG) & (USB_STA_PWR | USB_STA_TXE)) == USB_STA_PWR;
}

enum GDBError gdbWaitForWritable() {
    u32 timeout = 0;

    while (!gdbSerialCanWrite()) {
        if (++timeout == 8192) {
            return GDBErrorUSBTimeout;
        }
    }

    return GDBErrorNone;
}

enum GDBError gdbSerialInit(OSPiHandle* handler, OSMesgQueue* dmaMessageQ)
{
    gdbSerialHandle = *handler;

    gdbSerialHandle.latency = 0x04;
    gdbSerialHandle.pulse = 0x0C;

    OSIntMask prev = osGetIntMask();
    osSetIntMask(0);
    gdbSerialHandle.next = __osPiTable;
    __osPiTable = &gdbSerialHandle;
    osSetIntMask(prev);

    __gdbDmaMessageQ = dmaMessageQ;

    osCreateMesgQueue(&gdbSerialSemaphore, &gdbSerialSemaphoreMsg, 1);

    gdbWriteReg(GDB_EV_REGISTER_KEY, 0xAA55);
    gdbWriteReg(GDB_EV_REGISTER_SYS_CFG, 0);
    gdbWriteReg(GDB_EV_REGISTER_USB_CFG, USB_CMD_RD_NOP);

    while (gdbSerialCanRead()) {
        gdbSerialRead(gdbSerialReadBuffer, GDB_USB_SERIAL_SIZE);
    }

    return GDBErrorNone;
}

enum GDBError gdbSerialRead(char* target, u32 len) {
    while (len) {
        int chunkSize = GDB_USB_SERIAL_SIZE;
        if (chunkSize > len) {
            chunkSize = len;
        }
        int baddr = GDB_USB_SERIAL_SIZE - chunkSize;

        gdbWriteReg(GDB_EV_REGISTER_USB_CFG, USB_CMD_RD | baddr);

        enum GDBError err = gdbUsbBusy();
        if (err != GDBErrorNone) return err;

        err = gdbDMARead(target, REG_ADDR(GDB_EV_REGISTER_USB_DATA + baddr), chunkSize);
        if (err != GDBErrorNone) return err;

        target += chunkSize;
        len -= chunkSize;
    }

    return GDBErrorNone;
}

enum GDBError gdbSerialWrite(char* src, u32 len) {
    enum GDBError err = gdbWaitForWritable();
    if (err != GDBErrorNone) return err;

    gdbWriteReg(GDB_EV_REGISTER_USB_CFG, USB_CMD_WR_NOP);

    while (len) {
        int chunkSize = GDB_USB_SERIAL_SIZE;
        if (chunkSize > len) {
            chunkSize = len;
        }
        int baddr = GDB_USB_SERIAL_SIZE - chunkSize;
        err = gdbDMAWrite(src, REG_ADDR(GDB_EV_REGISTER_USB_DATA + baddr), chunkSize);
        if (err != GDBErrorNone) return err;

        gdbWriteReg(GDB_EV_REGISTER_USB_CFG, USB_CMD_WR | baddr);

        err = gdbUsbBusy();
        if (err != GDBErrorNone) return err;

        src += chunkSize;
        len -= chunkSize;
    }

    return GDBErrorNone;
}

enum GDBError __gdbSendMessage(enum GDBDataType type, char* src, u32 len) {
    if (len >= 0x1000000) {
        return GDBErrorMessageTooLong;
    }

    u32 header = (type << 24) | (0xFFFFFF & len);
    strcpy(gdbSerialSendBuffer, gdbHeaderText);
    memcpy(gdbSerialSendBuffer + HEADER_TEXT_LENGTH, (char*)&header, sizeof(u32));

    u32 firstChunkLength = len;

    if (firstChunkLength > GDB_USB_SERIAL_SIZE - MESSAGE_HEADER_SIZE) {
        firstChunkLength = GDB_USB_SERIAL_SIZE - MESSAGE_HEADER_SIZE;
    }

    enum GDBError err;

    memcpy(gdbSerialSendBuffer + MESSAGE_HEADER_SIZE, src, firstChunkLength);

    if (GDB_USB_SERIAL_SIZE >= MESSAGE_HEADER_SIZE + firstChunkLength + MESSAGE_FOOTER_SIZE) {
        // entire message fits into a single 512 byte buffer
        strcpy(gdbSerialSendBuffer + MESSAGE_HEADER_SIZE + firstChunkLength, gdbFooterText);
        err = gdbSerialWrite(gdbSerialSendBuffer, ALIGN_2_BYTES(MESSAGE_HEADER_SIZE + firstChunkLength + MESSAGE_FOOTER_SIZE));
        if (err != GDBErrorNone) return err;
    } else {
        // header partially fits
        if (GDB_USB_SERIAL_SIZE > MESSAGE_HEADER_SIZE + firstChunkLength) {
            println("Partial header");
            memcpy(gdbSerialSendBuffer + MESSAGE_HEADER_SIZE + firstChunkLength, gdbFooterText, GDB_USB_SERIAL_SIZE - MESSAGE_HEADER_SIZE - firstChunkLength);
        }

        err = gdbSerialWrite(gdbSerialSendBuffer, GDB_USB_SERIAL_SIZE);
        if (err != GDBErrorNone) return err;
        src += firstChunkLength;
        len -= firstChunkLength;

        while (len >= GDB_USB_SERIAL_SIZE) {
            println("Sending full chunk");
            if ((int)src == ((int)src & !0x7)) {
                err = gdbSerialWrite(src, GDB_USB_SERIAL_SIZE);
            } else {
                memcpy(gdbSerialSendBuffer, src, GDB_USB_SERIAL_SIZE);
                gdbSerialWrite(gdbSerialSendBuffer, GDB_USB_SERIAL_SIZE);
            }
            if (err != GDBErrorNone) return err;
            src += GDB_USB_SERIAL_SIZE;
            len -= GDB_USB_SERIAL_SIZE;
        }

        if (len) {
            memcpy(gdbSerialSendBuffer, src, len);
        }

        if (len + MESSAGE_FOOTER_SIZE <= GDB_USB_SERIAL_SIZE) {
            strcpy(gdbSerialSendBuffer + len, gdbFooterText);
            err = gdbSerialWrite(gdbSerialSendBuffer, len + MESSAGE_FOOTER_SIZE);
            if (err != GDBErrorNone) return err;
        } else {
            memcpy(gdbSerialSendBuffer + len, gdbFooterText, GDB_USB_SERIAL_SIZE - len);
            err = gdbSerialWrite(gdbSerialSendBuffer, GDB_USB_SERIAL_SIZE);
            if (err != GDBErrorNone) return err;
            strcpy(gdbSerialSendBuffer, &gdbFooterText[GDB_USB_SERIAL_SIZE - len]);
            err = gdbSerialWrite(gdbSerialSendBuffer, MESSAGE_FOOTER_SIZE + len - GDB_USB_SERIAL_SIZE);
            if (err != GDBErrorNone) return err;
        }
    }

    return GDBErrorNone;
}

enum GDBError gdbSendMessage(enum GDBDataType type, char* src, u32 len) {
    OSMesg msg = 0;
    osSendMesg(&gdbSerialSemaphore, msg, OS_MESG_BLOCK);
    enum GDBError result = __gdbSendMessage(type, src, len);
    osRecvMesg(&gdbSerialSemaphore, &msg, OS_MESG_NOBLOCK);
    return result;
}

enum GDBError gdbPollMessageHeader(enum GDBDataType* type, u32* len)
{
    if (!gdbSerialCanRead()) {
        return GDBErrorUSBNoData;
    }

    enum GDBError err = gdbSerialRead(gdbSerialReadBuffer, USB_MIN_SIZE);
    if (err != GDBErrorNone)  return err;

    if (strncmp(gdbSerialReadBuffer, gdbHeaderText, HEADER_TEXT_LENGTH) == 0) {
        *type = gdbSerialReadBuffer[4];
        *len = 0xFFFFFF & *((u32*)&gdbSerialReadBuffer[4]);
        return GDBErrorNone;
    }

    return GDBErrorUSBNoData;
}

enum GDBError gdbReadMessage(char* target, u32 len)
{
    u32 chunkSize = len;
    u32 initialLen = len;
    
    if (chunkSize > USB_MIN_SIZE - MESSAGE_HEADER_SIZE) {
        chunkSize = USB_MIN_SIZE - MESSAGE_HEADER_SIZE;
    }

    // copy the last 0-8 bytes of data from when the header was read
    memcpy(target, &gdbSerialReadBuffer[MESSAGE_HEADER_SIZE], chunkSize);
    target += chunkSize;
    len -= chunkSize;

    // check the footer for messages 0-4 characters long
    if (USB_MIN_SIZE >= MESSAGE_FOOTER_SIZE + MESSAGE_HEADER_SIZE + chunkSize) {
        return strncmp(&gdbSerialReadBuffer[MESSAGE_HEADER_SIZE + chunkSize], gdbFooterText, MESSAGE_FOOTER_SIZE) == 0 ? GDBErrorNone : GDBErrorBadFooter;
    }

    enum GDBError err;

    // read data in chunks of 512 bytes
    while (len > GDB_USB_SERIAL_SIZE) {
        // check if target is aligned for direct dma
        if ((u32)target == ((u32)target & ~0x7)) {
            err = gdbSerialRead(target, GDB_USB_SERIAL_SIZE);
            if (err != GDBErrorNone)  return err;
        } else {
            err = gdbSerialRead(gdbSerialReadBuffer, GDB_USB_SERIAL_SIZE);
            if (err != GDBErrorNone)  return err;
            memcpy(target, gdbSerialReadBuffer, GDB_USB_SERIAL_SIZE);
        }
        target += GDB_USB_SERIAL_SIZE;
        len -= GDB_USB_SERIAL_SIZE;
    }

    u32 footerStart;

    if (len > 0) {
        chunkSize = ALIGN_16_BYTES(len + MESSAGE_FOOTER_SIZE);
        if (chunkSize > GDB_USB_SERIAL_SIZE) {
            chunkSize = GDB_USB_SERIAL_SIZE;
        }

        err = gdbSerialRead(gdbSerialReadBuffer, chunkSize);
        if (err != GDBErrorNone)  return err;
        memcpy(target, gdbSerialReadBuffer, len);

        footerStart = len;
        chunkSize -= len;
    } else {
        if (initialLen + MESSAGE_HEADER_SIZE < USB_MIN_SIZE) {
            chunkSize = USB_MIN_SIZE - initialLen - MESSAGE_HEADER_SIZE;
            footerStart = MESSAGE_HEADER_SIZE + initialLen;
        } else {
            chunkSize = 0;
            footerStart = 0;
        }
    }


    if (chunkSize > MESSAGE_FOOTER_SIZE) {
        chunkSize = MESSAGE_FOOTER_SIZE;
    }

    if (chunkSize > 0 && strncmp(&gdbSerialReadBuffer[footerStart], gdbFooterText, chunkSize) != 0) {
        return GDBErrorBadFooter;
    }

    if (chunkSize < MESSAGE_FOOTER_SIZE) {
        err = gdbSerialRead(gdbSerialReadBuffer, USB_MIN_SIZE);
        if (err != GDBErrorNone)  return err;
        
        if (strncmp(gdbSerialReadBuffer, &gdbFooterText[chunkSize], MESSAGE_FOOTER_SIZE - chunkSize) != 0) {
            return GDBErrorBadFooter;
        }
    }

    return GDBErrorNone;
}

enum GDBError __gdbPollMessage(enum GDBDataType* type, char* target, u32* len, u32 maxLen) {
    enum GDBError err = gdbPollMessageHeader(type, len);
    if (err != GDBErrorNone)  return err;

    if (*len > maxLen) {
        // TODO flush buffer
        return GDBErrorBufferTooSmall;
    }

    err = gdbReadMessage(target, *len);
    if (err != GDBErrorNone)  return err;

    return GDBErrorNone;
}

enum GDBError gdbPollMessage(enum GDBDataType* type, char* target, u32* len, u32 maxLen) {
    OSMesg msg = 0;
    osSendMesg(&gdbSerialSemaphore, msg, OS_MESG_BLOCK);
    enum GDBError result = __gdbPollMessage(type, target, len, maxLen);
    osRecvMesg(&gdbSerialSemaphore, &msg, OS_MESG_NOBLOCK);
    return result;
}