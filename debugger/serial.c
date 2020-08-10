
#include <ultra64.h>
#include <string.h>
#include "serial.h"

static OSPiHandle* __gdbHandler;
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
#define MESSAGE_FOOTER_SIZE     8

extern void println(char* text);

// used to ensure that the memory buffers are aligned to 8 bytes
long long __gdbUnusedAlign;
static char gdbSerialSendBuffer[GDB_USB_SERIAL_SIZE];
static char gdbSerialReadBuffer[GDB_USB_SERIAL_SIZE];
static u32 gdbReadPosition = GDB_USB_SERIAL_SIZE;
static u32 gdbHeaderIndexPos = 0;

static char gdbHeaderText[] = "DMA@";
static char gdbFooterText[] = "CMPH";

struct GDBMessageHeader {
    u32 type: 8;
    u32 length: 24;
};

static union {
    char asBytes[4];
    struct GDBMessageHeader asHeader;
} gdbReadHeader;

enum GDBEVRegister {
    GDB_EV_REGISTER_USB_CFG = 0x0004,
    GDB_EV_REGISTER_USB_TIMER = 0x000C,
    GDB_EV_REGISTER_USB_DATA = 0x0400,
    GDB_EV_REGISTER_SYS_CFG =0x8000,
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
    if (osEPiStartDma(__gdbHandler, &dmaIoMesgBuf, OS_READ) == -1)
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
    if (osEPiStartDma(__gdbHandler, &dmaIoMesgBuf, OS_WRITE) == -1)
    {
        return GDBErrorDMA;
    }

    osRecvMesg(__gdbDmaMessageQ, NULL, OS_MESG_BLOCK);

    return GDBErrorNone;
}

enum GDBError gdbReadReg(enum GDBEVRegister reg, u32* result) {
    union {
        long long __align;
        u32 alignedResult;
    } uResult;
    enum GDBError err = gdbDMARead(&uResult.alignedResult, REG_ADDR(reg), sizeof(u32));
    *result = uResult.alignedResult;
    return err;
}

enum GDBError gdbWriteReg(enum GDBEVRegister reg, u32 value) {
    union {
        long long __align;
        u32 alignedResult;
    } uResult;
    uResult.alignedResult = value;
    return gdbDMAWrite(&uResult.alignedResult, REG_ADDR(reg), sizeof(u32));
}

enum GDBError gdbUsbBusy() {
    u32 tout = 0;
    enum GDBError err;
    u32 registerValue;

    do {
        err = gdbReadReg(GDB_EV_REGISTER_USB_CFG, &registerValue);
        if (err != GDBErrorNone) return err;
        if (tout++ > 8192) {
            err = gdbWriteReg(GDB_EV_REGISTER_USB_CFG, USB_CMD_RD_NOP);
            if (err != GDBErrorNone) return err;
            return GDBErrorUSBTimeout;
        }
    } while (registerValue & USB_STA_ACT != 0);

    return GDBErrorNone;
}

u8 gdbSerialCanRead() {
    u32 status;
    return gdbReadReg(GDB_EV_REGISTER_USB_CFG, &status) == GDBErrorNone && (status & (USB_STA_PWR | USB_STA_RXF)) == USB_STA_PWR;
}

enum GDBError gdbSerialInit(OSPiHandle* handler, OSMesgQueue* dmaMessageQ)
{
    __gdbHandler = handler;
    __gdbDmaMessageQ = dmaMessageQ;

    enum GDBError err = gdbWriteReg(GDB_EV_REGISTER_KEY, 0xAA55);
    if (err != GDBErrorNone) return err;
    err = gdbWriteReg(GDB_EV_REGISTER_SYS_CFG, 0);
    if (err != GDBErrorNone) return err;
    return gdbWriteReg(GDB_EV_REGISTER_USB_CFG, USB_CMD_RD_NOP);
}

enum GDBError gdbSerialRead(char* target, u32 len) {
    // byte align to 2
    // len = (len + 1) & ~1;

    while (len) {
        int chunkSize = GDB_USB_SERIAL_SIZE;
        if (chunkSize > len) {
            chunkSize = len;
        }
        int baddr = GDB_USB_SERIAL_SIZE - chunkSize;

        enum GDBError err = gdbWriteReg(GDB_EV_REGISTER_USB_CFG, USB_CMD_RD | baddr);
        if (err != GDBErrorNone) return err;

        err = gdbUsbBusy();
        if (err != GDBErrorNone) return err;

        err = gdbDMARead(target, REG_ADDR(GDB_EV_REGISTER_USB_DATA + baddr), chunkSize);
        if (err != GDBErrorNone) return err;

        target += chunkSize;
        len -= chunkSize;
    }

    return GDBErrorNone;
}

enum GDBError gdbSerialWrite(char* src, u32 len) {
    // byte align to 2
    // len = (len + 1) & ~1;

    gdbWriteReg(GDB_EV_REGISTER_USB_CFG, USB_CMD_WR_NOP);

    while (len) {
        int chunkSize = GDB_USB_SERIAL_SIZE;
        if (chunkSize > len) {
            chunkSize = len;
        }
        int baddr = GDB_USB_SERIAL_SIZE - chunkSize;

        enum GDBError err = gdbDMAWrite(src, REG_ADDR(GDB_EV_REGISTER_USB_DATA + baddr), chunkSize);
        if (err != GDBErrorNone) return err;

        err = gdbWriteReg(GDB_EV_REGISTER_USB_CFG, USB_CMD_WR | chunkSize);
        if (err != GDBErrorNone) return err;

        err = gdbUsbBusy();
        if (err != GDBErrorNone) return err;

        src += chunkSize;
        len -= chunkSize;
    }

    return GDBErrorNone;
}

enum GDBError gdbSendMessage(enum GDBDataType type, char* src, u32 len) {
    struct GDBMessageHeader header;
    enum GDBError err;

    if (len >= 0x1000000) {
        return GDBErrorMessageTooLong;
    }

    header.type = type;
    header.length = len;    

    strcpy(gdbSerialSendBuffer, gdbHeaderText);
    strncpy(gdbSerialSendBuffer + strlen("DMA@"), (char*)&header, sizeof(struct GDBMessageHeader));

    u32 firstChunkLength = len;

    if (firstChunkLength > GDB_USB_SERIAL_SIZE - MESSAGE_HEADER_SIZE) {
        firstChunkLength = GDB_USB_SERIAL_SIZE - MESSAGE_HEADER_SIZE;
    }

    strncpy(gdbSerialSendBuffer + MESSAGE_HEADER_SIZE, src, firstChunkLength);

    s32 cmphLength = (s32)GDB_USB_SERIAL_SIZE - firstChunkLength - MESSAGE_HEADER_SIZE;

    if (cmphLength >= MESSAGE_FOOTER_SIZE) {
        strcpy(gdbSerialSendBuffer + MESSAGE_HEADER_SIZE + firstChunkLength, gdbFooterText);
        err = gdbSerialWrite(gdbSerialSendBuffer, GDB_USB_SERIAL_SIZE + firstChunkLength + MESSAGE_FOOTER_SIZE);
        if (err != GDBErrorNone) return err;
    } else {
        err = gdbSerialWrite(gdbSerialSendBuffer, GDB_USB_SERIAL_SIZE + firstChunkLength + MESSAGE_FOOTER_SIZE);
        if (err != GDBErrorNone) return err;
        src += firstChunkLength;
        len -= firstChunkLength;

        while (len >= GDB_USB_SERIAL_SIZE) {
            err = gdbSerialWrite(src, GDB_USB_SERIAL_SIZE);
            if (err != GDBErrorNone) return err;
            src += GDB_USB_SERIAL_SIZE;
            len -= GDB_USB_SERIAL_SIZE;
        }

        strncpy(gdbSerialSendBuffer, src, len);

        if (len + MESSAGE_FOOTER_SIZE <= GDB_USB_SERIAL_SIZE) {
            strcpy(gdbSerialSendBuffer + len, gdbFooterText);
            err = gdbSerialWrite(gdbSerialSendBuffer, len + MESSAGE_FOOTER_SIZE);
            if (err != GDBErrorNone) return err;
        } else {
            strncpy(gdbSerialSendBuffer + len, gdbFooterText, GDB_USB_SERIAL_SIZE - len);
            err = gdbSerialWrite(gdbSerialSendBuffer, GDB_USB_SERIAL_SIZE);
            if (err != GDBErrorNone) return err;
            strcpy(gdbSerialSendBuffer, &gdbFooterText[GDB_USB_SERIAL_SIZE - len]);
            err = gdbSerialWrite(gdbSerialSendBuffer, MESSAGE_FOOTER_SIZE + len - GDB_USB_SERIAL_SIZE);
            if (err != GDBErrorNone) return err;
        }
    }

    return GDBErrorNone;
}

enum GDBError gdbCheckReadHead() {
    if (gdbReadPosition == GDB_USB_SERIAL_SIZE) {
        if (gdbSerialCanRead()) {
            enum GDBError result = gdbSerialRead(gdbSerialReadBuffer, GDB_USB_SERIAL_SIZE);
            gdbReadPosition = 0;
            return result;
        } else {
            return GDBErrorUSBNoData;
        }
    } else {
        return GDBErrorNone;
    }
}

enum GDBError gdbPollMessageHeader(enum GDBDataType* type, u32* len)
{
    for (int repeat = 0; repeat < 2; ++repeat) {
        // finish checking for messages in
        // the current chunk first
        while (gdbReadPosition < GDB_USB_SERIAL_SIZE) {
            if (gdbHeaderIndexPos >= HEADER_TEXT_LENGTH) {
                gdbReadHeader.asBytes[gdbHeaderIndexPos - HEADER_TEXT_LENGTH] = gdbSerialReadBuffer[gdbReadPosition];

                if (gdbHeaderIndexPos == MESSAGE_HEADER_SIZE) {
                    *type = gdbReadHeader.asHeader.type;
                    *len = gdbReadHeader.asHeader.length;
                    ++gdbReadPosition;
                    return GDBErrorNone;
                }
            }

            if (gdbHeaderText[gdbHeaderIndexPos] == gdbSerialReadBuffer[gdbReadPosition]) {
                ++gdbHeaderIndexPos;

                if (gdbHeaderIndexPos == HEADER_TEXT_LENGTH) {
                    gdbHeaderIndexPos = 0;
                }
            } else {
                gdbHeaderIndexPos = 0;
            }

            ++gdbReadPosition;
        }

        // check for messages in the next chunk second
        enum GDBError err = gdbCheckReadHead();
        if (err != GDBErrorNone)  return err;
    }

    return GDBErrorUSBNoData;
}

enum GDBError gdbReadMessage(char* target, u32 len)
{
    enum GDBError err = gdbCheckReadHead();
    if (err != GDBErrorNone)  return err;

    u32 firstPacketSize = len;

    if (firstPacketSize > GDB_USB_SERIAL_SIZE - gdbReadPosition) {
        firstPacketSize = GDB_USB_SERIAL_SIZE - gdbReadPosition;
    }

    strncpy(target, &gdbSerialReadBuffer[gdbReadPosition], firstPacketSize);
    target += firstPacketSize;
    len -= firstPacketSize;
    gdbReadPosition += firstPacketSize;

    while (len > GDB_USB_SERIAL_SIZE) {
        // check if target is aligned for direct dma
        if ((u32)target == ((u32)target & ~0x7)) {
            err = gdbSerialRead(target, GDB_USB_SERIAL_SIZE);
            if (err != GDBErrorNone)  return err;
        } else {
            err = gdbSerialRead(gdbSerialReadBuffer, GDB_USB_SERIAL_SIZE);
            if (err != GDBErrorNone)  return err;
            strncpy(target, gdbSerialReadBuffer, GDB_USB_SERIAL_SIZE);
        }
        target += GDB_USB_SERIAL_SIZE;
        len -= GDB_USB_SERIAL_SIZE;
    }

    err = gdbCheckReadHead();
    if (err != GDBErrorNone)  return err;

    strncpy(target, &gdbSerialReadBuffer[gdbReadPosition], len);
    gdbReadPosition += len;

    int footerIndex;
    for (footerIndex = 0; footerIndex < MESSAGE_FOOTER_SIZE; ++footerIndex) {
        err = gdbCheckReadHead();
        if (err != GDBErrorNone)  return err;

        if (gdbFooterText[footerIndex] != gdbSerialReadBuffer[gdbReadPosition]) {
            return GDBErrorBadFooter;
        }
        ++gdbReadPosition;
    }

    return GDBErrorNone;
}