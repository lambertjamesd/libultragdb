
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

enum GDBCartType {
    GDBCartTypeNone,
    GDBCartTypeX7,
    GDBCartTypeCen64,
};

#define GDB_USB_SERIAL_SIZE 512

enum GDBError gdbSerialInit(OSPiHandle* handler, OSMesgQueue* dmaMessageQ);

extern u8 (*gdbSerialCanRead)();
extern enum GDBError (*gdbSerialRead)(char* target, u32 len);
extern enum GDBError (*gdbSerialWrite)(char* src, u32 len);
extern enum GDBCartType gdbCartType;

enum GDBError gdbSendMessage(enum GDBDataType type, char* src, u32 len);

enum GDBError gdbPollHeader(enum GDBDataType* type, u32* len);
enum GDBError gdbReadData(char* target, u32 len, u32* dataRead);
enum GDBError gdbFinishRead();

#endif