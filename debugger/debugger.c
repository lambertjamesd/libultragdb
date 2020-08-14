
#include "debugger.h"
#include <string.h>
#include <stdio.h>

#define MAX_PACKET_SIZE     0x4000
#define MAX_DEBUGGER_THREADS    8

#define GDB_ANY_THREAD      0
#define GDB_ALL_THREADS     -1  

static OSThread* gdbTargetThreads[MAX_DEBUGGER_THREADS];
static OSId gdbCurrentThreadG;
static OSId gdbCurrentThreadg;
static OSId gdbCurrentThreadc;
static char gdbPacketBuffer[MAX_PACKET_SIZE];
static char gdbOutputBuffer[MAX_PACKET_SIZE];
static int gdbNextBufferTarget;
static int gdbNextSearchIndex;

void println(char* text);

u32 gdbParseHex(char* src) {
    u32 result = 0;
    int currentChar;

    for (currentChar = 0; currentChar < 8; ++currentChar) {
        result = result << 4;
        if (*src >= 'a' && *src <= 'f') {
            result += 10 + *src - 'a';
        } else if (*src >= 'A' && *src <= 'F') {
            result += 10 + *src - 'A';
        } else if (*src >= '0' && *src <= '9') {
            result += *src - '0';
        } else {
            break;
        }

        ++src;
    }

    return result;
}

void gdbCopy(char* dst, char* src, int len)
{
    while (len)
    {
        *dst++ = *src++;
        --len;
    }
}

int gdbIsAlphaNum(int chr)
{
    return chr >= 'a' && chr <= 'z' || chr >= 'A' && chr <= 'Z' || chr >= '0' && chr <= '9';
}

int gdbApplyChecksum(char* message)
{
    char* messageStart = message;
    if (*message == '$') {
        ++message;
    }
    
    u8 checksum = 0;
    while (*message)
    {
        if (*message == '#') {
            ++message;
            break;
        }

        checksum += (u8)*message;
        ++message;
    }

    sprintf(message, "%02x", checksum);

    return (message - messageStart) + 2;
    return strlen(messageStart);
}

enum GDBError gdbParsePacket(char* input, u32 len, char **commandStart, char **packetEnd)
{
    char* stringEnd = input + len;
    while (input < stringEnd)
    {
        if (*input == '$') {
            ++input;
            *commandStart = input;
            break;
        }
        ++input;
    }

    while (input < stringEnd) {
        if (*input == '#') {
            *packetEnd = input;
            ++input;
            return GDBErrorNone;
        }
        else
        {
            ++input;
        }
    } 

    return GDBErrorBadPacket;
}

enum GDBError gdbSendStopReply() {
    char* current = gdbOutputBuffer;
    current += sprintf(current, "#T%02x", 0);

    int i;
    for (i = 0; i < MAX_DEBUGGER_THREADS; ++i) {
        if (gdbTargetThreads[i]) {
            current += sprintf(current, "thread:%d;", osGetThreadId(gdbTargetThreads[i]));
            // todo get CPU halt state
            // if (gdbTargetThreads[i]->state) {

            // }
        }
    }

    *current++ = '#';
    *current = '\0';

    return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
}

enum GDBError gdbHandleQuery(char* commandStart, char *packetEnd) {
    if (strncmp(commandStart, "qSupported", strlen("qSupported")) == 0) {
        strcpy(gdbOutputBuffer, "$PacketSize=4000;vContSupported+#");
        return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
    } else if (strncmp(commandStart, "qTStatus", strlen("qTStatus")) == 0) {
        strcpy(gdbOutputBuffer, "$T0#");
        return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
    } else if (strncmp(commandStart, "qfThreadInfo", strlen("qfThreadInfo")) == 0) {
        strcpy(gdbOutputBuffer, "$m");
        char* outputWrite = gdbOutputBuffer + 2;
        int i;
        int first = 1;
        for (i = 0; i < MAX_DEBUGGER_THREADS; ++i) {
            if (gdbTargetThreads[i]) {
                if (first) {    
                    first = 0;
                    outputWrite += sprintf(outputWrite, "%x", osGetThreadId(gdbTargetThreads[i]));
                } else {
                    outputWrite += sprintf(outputWrite, ",%x", osGetThreadId(gdbTargetThreads[i]));
                }
            }
        }
        *outputWrite++ = '#';
        *outputWrite++ = '\0';
        return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
    } else if (strncmp(commandStart, "qsThreadInfo", strlen("qsThreadInfo")) == 0) {
        strcpy(gdbOutputBuffer, "$l#");
        return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
    } else if (strncmp(commandStart, "qAttached", strlen("qAttached")) == 0) {
        strcpy(gdbOutputBuffer, "$1#");
        return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
    } else if (strncmp(commandStart, "qC", strlen("qC")) == 0) {
        strcpy(gdbOutputBuffer, "$QC");
        char* outputWrite = gdbOutputBuffer + 3;
        int i;
        for (i = 0; i < MAX_DEBUGGER_THREADS; ++i) {
            if (gdbTargetThreads[i]) {
                outputWrite += sprintf(outputWrite, "%x", osGetThreadId(gdbTargetThreads[i]));
                break;
            }
        }

        if (i == MAX_DEBUGGER_THREADS) {
            strcpy(gdbOutputBuffer, "$E00#");
        } else {
            *outputWrite++ = '#';
            *outputWrite++ = '\0';
        }
        return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
    }

    println(commandStart);

    return gdbSendMessage(GDBDataTypeGDB, "$#00", strlen("$#00"));
}

enum GDBError gdbHandleV(char* commandStart, char *packetEnd) {
    if (strncmp(commandStart, "vMustReplyEmpty", strlen("vMustReplyEmpty")) == 0) {
        return gdbSendMessage(GDBDataTypeGDB, "$#00", strlen("$#00"));
    }

    println(commandStart);

    return gdbSendMessage(GDBDataTypeGDB, "$#00", strlen("$#00"));
}

enum GDBError gdbHandlePacket(char* commandStart, char *packetEnd) {
    println(commandStart);
    switch (*commandStart) {
        case 'q':
            return gdbHandleQuery(commandStart, packetEnd);
        case 'v':
            return gdbHandleV(commandStart, packetEnd);
        case 'H':
        {
            OSId threadId;

            if (commandStart[2] == '-') {
                threadId = -1;
            } else {
                threadId = gdbParseHex(commandStart + 2);
            }

            switch (commandStart[1]) {
            case 'G':
                gdbCurrentThreadG = threadId;
                break;
            case 'g':
                gdbCurrentThreadg = threadId;
                break;
            case 'c':
                gdbCurrentThreadc = threadId;
                break;
            }

            return gdbSendMessage(GDBDataTypeGDB, "$OK#9a", strlen("$OK#9a"));
        }
        case '!':
            return gdbSendMessage(GDBDataTypeGDB, "$#00", strlen("$#00"));
        case '?':
            return gdbSendStopReply();
    }

    println(commandStart);

    return gdbSendMessage(GDBDataTypeGDB, "$#00", strlen("$#00"));
}

enum GDBError gdbCheckForPacket() {
    char* commandStart;
    char* packetEnd;

    if (gdbSerialCanRead()) {
        enum GDBDataType type;
        u32 len;
        enum GDBError err = gdbPollMessage(&type, gdbPacketBuffer, &len, MAX_PACKET_SIZE);
        gdbPacketBuffer[len] = '\0';
        if (err != GDBErrorNone) return err;

        if (type == GDBDataTypeGDB) {
            err = gdbParsePacket(gdbPacketBuffer, len, &commandStart, &packetEnd);
            if (err != GDBErrorNone) return err;

            err = gdbSendMessage(GDBDataTypeGDB, "+", strlen("+"));
            if (err != GDBErrorNone) return err;

            err = gdbHandlePacket(commandStart, packetEnd);
            if (err != GDBErrorNone) return err;
        }

        return GDBErrorNone;
    }

    return GDBErrorUSBNoData;
}

enum GDBError gdbInitDebugger(OSPiHandle* handler, OSMesgQueue* dmaMessageQ, OSThread** forThreads, u32 forThreadsLen)
{
    enum GDBError err = gdbSerialInit(handler, dmaMessageQ);
    if (err != GDBErrorNone) return err;

    OSThread* primaryThread = NULL;
    OSId currThread = osGetThreadId(NULL);

    int i;
    for (i = 0; i < forThreadsLen && i < MAX_DEBUGGER_THREADS; ++i) {
        gdbTargetThreads[i] = forThreads[i];

        if (osGetThreadId(forThreads[i]) == currThread) {
            primaryThread = forThreads[i];
        }
    }
    
    for (;i < MAX_DEBUGGER_THREADS; ++i) {
        gdbTargetThreads[i] = NULL;
    }

    // gdbCheckForPacket();
    
    return GDBErrorNone;

}