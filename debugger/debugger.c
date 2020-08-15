
#include "debugger.h"
#include <string.h>
#include <stdio.h>

#define MAX_PACKET_SIZE     0x4000
#define MAX_DEBUGGER_THREADS    8

#define GDB_ANY_THREAD      0
#define GDB_ALL_THREADS     -1 

#define strStartsWith(str, constStr) (strncmp(str, constStr, sizeof constStr - 1) == 0)

static OSThread* gdbTargetThreads[MAX_DEBUGGER_THREADS];
static OSId gdbCurrentThreadG;
static OSId gdbCurrentThreadg;
static OSId gdbCurrentThreadc;
static char gdbPacketBuffer[MAX_PACKET_SIZE];
static char gdbOutputBuffer[MAX_PACKET_SIZE];
static int gdbNextBufferTarget;
static int gdbNextSearchIndex;

void println(char* text);

OSThread* gdbFindThread(OSId id) {
    int i;
    for (i = 0; i < MAX_DEBUGGER_THREADS; ++i) {
        if (gdbTargetThreads[i] && (
            id == GDB_ANY_THREAD ||
            osGetThreadId(gdbTargetThreads[i]) == id
        )) {
            return gdbTargetThreads[i];
        }
    }
    return NULL;
}

u32 gdbParseHex(char* src, u32 maxBytes) {
    u32 result = 0;
    int currentChar;
    u32 maxCharacters = maxBytes * 2;

    for (currentChar = 0; currentChar < maxCharacters; ++currentChar) {
        if (*src >= 'a' && *src <= 'f') {
            result = (result << 4) + 10 + *src - 'a';
        } else if (*src >= 'A' && *src <= 'F') {
            result = (result << 4) + 10 + *src - 'A';
        } else if (*src >= '0' && *src <= '9') {
            result = (result << 4) + *src - '0';
        } else {
            break;
        }

        ++src;
    }

    return result;
}

static char gdbHexLetters[16] = "0123456789abcdef";

char* gdbWriteHex(char* target, u8* src, u32 bytes) {
    u32 i;
    for (i = 0; i < bytes; ++i) {
        *target++ = gdbHexLetters[(*src) >> 4];
        *target++ = gdbHexLetters[(*src) & 0xF];
        ++src;
    }
    return target;
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
    current += sprintf(current, "$T%02x", 0);

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

    return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
}

enum GDBError gdbReplyRegisters() {
    char* current = gdbOutputBuffer;
    *current++ = '$';

    OSThread* thread = gdbFindThread(gdbCurrentThreadg);

    if (thread) {
        /* 0~ GPR0-31(yes, include zero),[32]PS(status),LO,HI,BadVAddr,Cause,PC,[38]FPR0-31,[70]fpcs,fpir,[72]..(dsp?),[90]end */
        current += sprintf(current, "%08x%08x", 0, 0); // zero
        current = gdbWriteHex(current, (u8*)&thread->context, offsetof(__OSThreadContext, gp));
        current += sprintf(current, "%08x%08x", 0, 0); // k0
        current += sprintf(current, "%08x%08x", 0, 0); // k1
        current = gdbWriteHex(current, (u8*)&thread->context.gp, offsetof(__OSThreadContext, lo) - offsetof(__OSThreadContext, gp));

        current += sprintf(current, "%08x%08x", 0, thread->context.sr);
        current = gdbWriteHex(current, (u8*)&thread->context.lo, sizeof(u64) * 2);
        current += sprintf(current, "%08x%08x", 0, thread->context.badvaddr);
        current += sprintf(current, "%08x%08x", 0, thread->context.cause);
        current += sprintf(current, "%08x%08x", 0, thread->context.pc);

        current = gdbWriteHex(current, (u8*)&thread->context.fp0, sizeof(__OSThreadContext) - offsetof(__OSThreadContext, fp0));
        current += sprintf(current, "%08x%08x", 0, thread->context.fpcsr);
    }

    *current++ = '#';
    *current++ = '\0';

    return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
}

enum GDBError gdbReplyMemory(char* commandStart, char *packetEnd) {
    char* current = gdbOutputBuffer;
    *current++ = '$';

    char* lenText = commandStart + 1;

    while (*lenText != ',') {
        if (lenText == packetEnd) {
            return GDBErrorBadPacket;
        }
        ++lenText;
    }

    u8* dataSrc = (u8*)gdbParseHex(commandStart + 1, 4);
    u32 len = gdbParseHex(lenText + 1, 4);

    if ((u32)dataSrc < K0BASE) {
        while ((u32)dataSrc < K0BASE && len > 0) {
            *current++ = '0';
            *current++ = '0';
            ++dataSrc;
            --len;
        }
    }

    if (len > 0) {
        u32 maxLen;
        if ((u32)dataSrc < osMemSize + K0BASE) {
            maxLen = (osMemSize + K0BASE) - (u32)dataSrc;
        }

        if (len > maxLen) {
            len = maxLen;
        }

        current = gdbWriteHex(current, dataSrc, len);
    }

    *current++ = '#';
    *current++ = '\0';
    return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
}

enum GDBError gdbHandleQuery(char* commandStart, char *packetEnd) {
    if (strStartsWith(commandStart, "qSupported")) {
        strcpy(gdbOutputBuffer, "$PacketSize=4000;vContSupported+;swbreak+#");
        return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
    } else if (strStartsWith(commandStart, "qTStatus")) {
        strcpy(gdbOutputBuffer, "$T0#");
        return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
    } else if (strStartsWith(commandStart, "qfThreadInfo")) {
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
    } else if (strStartsWith(commandStart, "qsThreadInfo")) {
        strcpy(gdbOutputBuffer, "$l#");
        return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
    } else if (strStartsWith(commandStart, "qAttached")) {
        strcpy(gdbOutputBuffer, "$1#");
        return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
    } else if (strStartsWith(commandStart, "qC")) {
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
    } else if (strStartsWith(commandStart, "qTfV")) {
        return gdbSendMessage(GDBDataTypeGDB, "$#00", strlen("$#00"));
    } else if (strStartsWith(commandStart, "qTfP")) {
        return gdbSendMessage(GDBDataTypeGDB, "$#00", strlen("$#00"));
    } else if (strStartsWith(commandStart, "qOffsets")) {
        strcpy(gdbOutputBuffer, "$TextSeg=000#");
        return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
    } else if (strStartsWith(commandStart, "qSymbol")) {
        return gdbSendMessage(GDBDataTypeGDB, "$OK#9a", strlen("$OK#9a"));
    }

    println("Unknown q packet");
    println(commandStart);

    return gdbSendMessage(GDBDataTypeGDB, "$#00", strlen("$#00"));
}

enum GDBError gdbHandleV(char* commandStart, char *packetEnd) {
    if (strStartsWith(commandStart, "vMustReplyEmpty")) {
        return gdbSendMessage(GDBDataTypeGDB, "$#00", strlen("$#00"));
    }

    println("Unknown v packet");
    println(commandStart);

    return gdbSendMessage(GDBDataTypeGDB, "$#00", strlen("$#00"));
}

enum GDBError gdbHandlePacket(char* commandStart, char *packetEnd) {
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
                threadId = gdbParseHex(commandStart + 2, 4);
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
        case 'g':
            return gdbReplyRegisters();
        case 'm':
            return gdbReplyMemory(commandStart, packetEnd);
    }

    println("Unknown packet");
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