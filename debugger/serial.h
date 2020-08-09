
#ifndef __SERIAL_H
#define __SERIAL_H

#include <ultra64.h>

enum GDBError {
    GDBErrorNone,
    GDBErrorUSBTimeout,
    GDBErrorUSBNoData,
    GDBErrorBadPacket,
};

#define GDB_USB_SERIAL_SIZE 512

void gdbSerialInit(OSPiHandle* handler, OSMesgQueue* dmaMessageQ);

u8 gdbSerialCanRead();
enum GDBError gdbSerialRead(char* target, u32 len);
enum GDBError gdbSerialWrite(char* src, u32 len);

#endif