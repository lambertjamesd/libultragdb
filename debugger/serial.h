
#ifndef __SERIAL_H
#define __SERIAL_H

#include <ultra64.h>

enum GDBError {
    GDBErrorNone,
    GDBErrorUSBTimeout,
    GDBErrorUSBNoData,
    GDBErrorBadPacket,
    GDBErrorMessageTooLong,
    GDBErrorBadFooter,
};

enum GDBDataType {
    GDBDataTypeNone,
    GDBDataTypeText,
    GDBDataTypeRawBinary,
    GDBDataTypeScreenshot,
    GDBDataTypeGDB,
};

#define GDB_USB_SERIAL_SIZE 512

void gdbSerialInit(OSPiHandle* handler, OSMesgQueue* dmaMessageQ);

u8 gdbSerialCanRead();
enum GDBError gdbSerialRead(char* target, u32 len);
enum GDBError gdbSerialWrite(char* src, u32 len);

enum GDBError gdbSendMessage(enum GDBDataType type, char* src, u32 len);
enum GDBError gdbPollMessageHeader(enum GDBDataType* type, u32* len);
enum GDBError gdbReadMessage(char* target, u32 len);

#endif