
#include <ultra64.h>
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

extern void println(char* text);

enum GDBEVRegister {
    GDB_EV_REGISTER_USB_CFG = 0x0004,
    GDB_EV_REGISTER_USB_TIMER = 0x000C,
    GDB_EV_REGISTER_USB_DATA = 0x0400,
    GDB_EV_REGISTER_SYS_CFG =0x8000,
    GDB_EV_REGISTER_KEY = 0x8004,
};

s32 gdbDMARead(void* ram, u32 piAddress, u32 len) {
	OSIoMesg dmaIoMesgBuf;

    dmaIoMesgBuf.hdr.pri = OS_MESG_PRI_NORMAL;
    dmaIoMesgBuf.hdr.retQueue = __gdbDmaMessageQ;
    dmaIoMesgBuf.dramAddr = ram;
    dmaIoMesgBuf.devAddr = piAddress & 0x1FFFFFFF;
    dmaIoMesgBuf.size = len;

    osInvalDCache(ram, len);
    if (osEPiStartDma(__gdbHandler, &dmaIoMesgBuf, OS_READ) == -1)
    {
        return -1;
    }

    osRecvMesg(__gdbDmaMessageQ, NULL, OS_MESG_BLOCK);

    return 0;
}

s32 gdbDMAWrite(void* ram, u32 piAddress, u32 len) {
	OSIoMesg dmaIoMesgBuf;

    dmaIoMesgBuf.hdr.pri = OS_MESG_PRI_NORMAL;
    dmaIoMesgBuf.hdr.retQueue = __gdbDmaMessageQ;
    dmaIoMesgBuf.dramAddr = ram;
    dmaIoMesgBuf.devAddr = piAddress & 0x1FFFFFFF;
    dmaIoMesgBuf.size = len;

    osWritebackDCache(ram, len);
    if (osEPiStartDma(__gdbHandler, &dmaIoMesgBuf, OS_WRITE) == -1)
    {
        return -1;
    }

    osRecvMesg(__gdbDmaMessageQ, NULL, OS_MESG_BLOCK);

    return 0;
}

u32 gdbReadReg(enum GDBEVRegister reg) {
    u32 result;
    gdbDMARead(&result, REG_ADDR(reg), sizeof(u32));
    return result;
}

void gdbWriteReg(enum GDBEVRegister reg, u32 value) {
    gdbDMAWrite(&value, REG_ADDR(reg), sizeof(u32));
}

enum GDBError gdbUsbBusy() {
    enum GDBError tout = 0;

    while (gdbReadReg(GDB_EV_REGISTER_USB_CFG) & USB_STA_ACT != 0) {
        if (tout++ == 8192) {
            gdbWriteReg(GDB_EV_REGISTER_USB_CFG, USB_CMD_RD_NOP);
            return GDBErrorUSBTimeout;
        }
    }

    return GDBErrorNone;
}

u8 gdbSerialCanRead() {
    u32 status = gdbReadReg(GDB_EV_REGISTER_USB_CFG) & (USB_STA_PWR | USB_STA_RXF);
    if (status == USB_STA_PWR) {
        return 1;
    }
    return 0;
}

void gdbSerialInit(OSPiHandle* handler, OSMesgQueue* dmaMessageQ)
{
    __gdbHandler = handler;
    __gdbDmaMessageQ = dmaMessageQ;

    gdbWriteReg(GDB_EV_REGISTER_KEY, 0xAA55);
    gdbWriteReg(GDB_EV_REGISTER_SYS_CFG, 0);
    gdbWriteReg(GDB_EV_REGISTER_USB_CFG, USB_CMD_RD_NOP);
}

enum GDBError gdbSerialRead(char* target, u32 len) {
    while (len) {
        int chunkSize = GDB_USB_SERIAL_SIZE;
        if (chunkSize > len) {
            chunkSize = len;
        }
        int baddr = GDB_USB_SERIAL_SIZE - chunkSize;

        gdbWriteReg(GDB_EV_REGISTER_USB_CFG, USB_CMD_RD | baddr);

        enum GDBError busyWait = gdbUsbBusy();

        if (busyWait != GDBErrorNone) {
            return busyWait;
        }

        gdbDMARead(target, REG_ADDR(GDB_EV_REGISTER_USB_DATA + baddr), chunkSize);

        target += chunkSize;
        len -= chunkSize;
    }

    return GDBErrorNone;
}

enum GDBError gdbSerialWrite(char* src, u32 len) {
    gdbWriteReg(GDB_EV_REGISTER_USB_CFG, USB_CMD_WR_NOP);

    while (len) {
        int chunkSize = GDB_USB_SERIAL_SIZE;
        if (chunkSize > len) {
            chunkSize = len;
        }
        int baddr = GDB_USB_SERIAL_SIZE - chunkSize;

        gdbDMAWrite(src, REG_ADDR(GDB_EV_REGISTER_USB_DATA + baddr), chunkSize);
        gdbWriteReg(GDB_EV_REGISTER_USB_CFG, USB_CMD_WR | chunkSize);

        enum GDBError busyWait = gdbUsbBusy();

        if (busyWait != GDBErrorNone) {
            return busyWait;
        }

        src += chunkSize;
        len -= chunkSize;
    }

    return GDBErrorNone;
}