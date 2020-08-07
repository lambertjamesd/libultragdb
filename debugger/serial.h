
#ifndef __SERIAL_H
#define __SERIAL_H

#include <ultra64.h>

enum GDBError {
    GDBErrorNone,
    GDBErrorUSBTimeout,
    GDBErrorUSBNoData,
};

void gdbSerialInit(OSPiHandle* handler, OSMesgQueue* dmaMessageQ);

enum GDBError gdbSerialRead(char* target, u32 len);
enum GDBError gdbSerialWrite(char* src, u32 len);

#endif