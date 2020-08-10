
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
    GDBErrorDMA,
    GDBErrorBufferTooSmall,
};

enum GDBDataType {
    GDBDataTypeNone,
    GDBDataTypeText,
    GDBDataTypeRawBinary,
    GDBDataTypeScreenshot,
    GDBDataTypeGDB,
};

#define GDB_USB_SERIAL_SIZE 512

enum GDBError gdbSerialInit(OSPiHandle* handler, OSMesgQueue* dmaMessageQ);

u8 gdbSerialCanRead();
enum GDBError gdbSerialRead(char* target, u32 len);
enum GDBError gdbSerialWrite(char* src, u32 len);

enum GDBError gdbSendMessage(enum GDBDataType type, char* src, u32 len);
enum GDBError gdbPollMessage(enum GDBDataType* type, char* target, u32* len, u32 maxLen);

#endif