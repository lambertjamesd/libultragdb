
#include "debugger.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define MAX_PACKET_SIZE     0x4000
#define MAX_DEBUGGER_THREADS    8

#define GDB_ANY_THREAD      0
#define GDB_ALL_THREADS     -1 

#define GDB_STACKSIZE           0x400
#define GDB_DEBUGGER_THREAD_ID  0xDBDB
#define GDB_CHECKS_PER_SEC      10

#define GDB_IS_ATTACHED         (1 << 0)
#define GDB_IS_WAITING_STOP     (1 << 1)

#define GDB_BREAK_INSTRUCTION(code) (0x0000000D | (((code) & 0xfffff) << 6))
#define GDB_TRAP_INSTRUCTION(code) (0x00000034 | (((code) & 0x3ff) << 6))
#define GDB_GET_TRAP_CODE(instr) (((instr) >> 6) & 0x3ff)

extern OSThread *	__osGetCurrFaultedThread(void);
extern OSThread *	__osGetNextFaultedThread(OSThread *);

// defined by makerom
extern char     _codeSegmentDataStart[];
extern char     _codeSegmentTextStart[];

#define strStartsWith(str, constStr) (strncmp(str, constStr, sizeof constStr - 1) == 0)

static OSThread* gdbTargetThreads[MAX_DEBUGGER_THREADS];
static OSThread* gdbManualBreak;
static OSId gdbCurrentThreadG;
static OSId gdbCurrentThreadg;
static OSId gdbCurrentThreadc;
static char gdbPacketBuffer[MAX_PACKET_SIZE];
static char gdbOutputBuffer[MAX_PACKET_SIZE];
static int gdbNextBufferTarget;
static int gdbNextSearchIndex;
static int gdbRunFlags;

static OSThread gdbDebuggerThread;
static u64 gdbDebuggerThreadStack[GDB_STACKSIZE/sizeof(u64)];

static OSTimer gdbPollTimer;
static OSMesgQueue gdbPollMesgQ;
static OSMesg gdbPollMesgQMessage;

static struct GDBBreakpoint gdbBreakpoints[GDB_MAX_BREAK_POINTS];

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

OSThread* gdbNextThread(OSThread* curr, OSId id) {
    if (id == GDB_ALL_THREADS) {
        int i;
        for (i = 0; i < MAX_DEBUGGER_THREADS; ++i) {
            if (curr == NULL && gdbTargetThreads[i]) {
                return gdbTargetThreads[i];
            } else if (curr != NULL && gdbTargetThreads[i] == curr) {
                curr = NULL;
            }
        }

        return NULL;
    } else {
        return gdbFindThread(id);
    }
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

OSId gdbParseThreadId(char* src) {
    if (src[0] == '-') {
        return GDB_ALL_THREADS;
    } else {
        return gdbParseHex(src, 4);
    }
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

struct GDBBreakpoint* gdbFindBreakpoint(u32 addr) {
    int i;
    struct GDBBreakpoint* firstEmpty = NULL;
    for (i = 0; i < GDB_MAX_BREAK_POINTS; ++i) {
        if (!firstEmpty && gdbBreakpoints[i].type == GDBBreakpointTypeNone) {
            firstEmpty = &gdbBreakpoints[i];
        } else if (gdbBreakpoints[i].type != GDBBreakpointTypeNone && gdbBreakpoints[i].addr == addr) {
            return &gdbBreakpoints[i];
        }
    }

    return firstEmpty;
}

struct GDBBreakpoint* gdbInsertBreakPoint(u32 addr, enum GDBBreakpointType type) {
    struct GDBBreakpoint* result = gdbFindBreakpoint(addr);

    if (result) {
        if (result->type == GDBBreakpointTypeNone) {
            result->prevValue = *((u32*)addr);
            *((u32*)addr) = GDB_TRAP_INSTRUCTION(0);
            result->type = type;
        } else if (result->type < type) {
            result->type = type;
        }
    }

    return result;
}

void gdbDisableBreakpoint(struct GDBBreakpoint* breakpoint) {
    if (breakpoint && breakpoint->type == GDBBreakpointTypeUser) {
        *((u32*)breakpoint->addr) = breakpoint->prevValue;
        breakpoint->type = GDBBreakpointTypeUserUnapplied;
    }
}

void gdbRenableBreakpoint(struct GDBBreakpoint* breakpoint) {
    if (breakpoint && breakpoint->type == GDBBreakpointTypeUserUnapplied) {
        breakpoint->prevValue = *((u32*)breakpoint->addr);
        *((u32*)breakpoint->addr) = GDB_TRAP_INSTRUCTION(0);
        breakpoint->type = GDBBreakpointTypeUser;
    }
}

void gdbRemoveBreakpoint(struct GDBBreakpoint* brk) {
    if (brk && brk->type != GDBBreakpointTypeNone) {
        if (brk->type != GDBBreakpointTypeUserUnapplied) {
            *((u32*)brk->addr) = brk->prevValue;
        }
        brk->prevValue = 0;
        brk->addr = 0;
        brk->type = GDBBreakpointTypeNone;
    }
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

void gdbWaitForStop() {
    gdbRunFlags |= GDB_IS_WAITING_STOP;
}

enum GDBError gdbSendStopReply(u32 stopReason) {
    char* current = gdbOutputBuffer;
    current += sprintf(current, "$T%02x", stopReason);

    int repliedBreak = 0;

    int i;
    for (i = 0; i < MAX_DEBUGGER_THREADS; ++i) {
        if (gdbTargetThreads[i]) {
            if (gdbTargetThreads[i]->state == OS_STATE_STOPPED && !repliedBreak) {
                repliedBreak = 1;
                current += sprintf(current, "swbreak:");
            }

            current += sprintf(current, "thread:%d;", osGetThreadId(gdbTargetThreads[i]));
        }
    }
    *current++ = '#';
    *current++ = '\0';

    for (i = 0; i < GDB_MAX_BREAK_POINTS; ++i) {
        gdbRenableBreakpoint(&gdbBreakpoints[i]);
    }

    return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
}

void gdbResumeThread(OSThread* thread) {
    if (thread == gdbManualBreak) {
        gdbManualBreak = NULL;
    }

    gdbDisableBreakpoint(gdbFindBreakpoint(thread->context.pc));

    osStartThread(thread);
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
        // the elf file loaded by gdb has symbols relative to 0, actual code is relative to 0x80000000
        // this makes the debugger happy
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

        char* strEnd = gdbWriteHex(current, dataSrc, len);

        if (gdbManualBreak && gdbManualBreak->context.pc >= (u32)dataSrc && gdbManualBreak->context.pc + sizeof(u32) <= ((u32)dataSrc + len)) {
            u32 offset = gdbManualBreak->context.pc - (u32)dataSrc;
            u32 brk = GDB_BREAK_INSTRUCTION(0);
            gdbWriteHex(current + offset * 2, (u8*)&brk, sizeof(u32));
        }

        current = strEnd;
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
        return gdbSendMessage(GDBDataTypeGDB, "$#00", strlen("$#00"));
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
        strcpy(gdbOutputBuffer, "$0#");
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
        // 0x20 is a magic number. I don't know why but it makes the addresses line up correctly
        sprintf(gdbOutputBuffer, "$Text=%x;Data=%x;Bss=%x#", 0, 0, 0);
        return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
    } else if (strStartsWith(commandStart, "qSymbol")) {
        return gdbSendMessage(GDBDataTypeGDB, "$OK#9a", strlen("$OK#9a"));
    } else if (strStartsWith(commandStart, "qThreadExtraInfo")) {
        OSId threadId = gdbParseHex(commandStart + sizeof("qThreadExtraInfo"), 4);

        OSThread* thread = gdbFindThread(threadId);

        if (thread) {
            int strLen = sprintf(gdbOutputBuffer + 0x200, "state %d priority %d", thread->state, thread->priority);
            gdbOutputBuffer[0] = '$';
            char* nextOut = gdbWriteHex(gdbOutputBuffer + 1, gdbOutputBuffer + 0x200, strLen);
            *nextOut++ = '#';
        return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
        } else {
            return gdbSendMessage(GDBDataTypeGDB, "$#00", strlen("$#00"));
        }
    }

    println("Unknown q packet");
    println(commandStart);

    return gdbSendMessage(GDBDataTypeGDB, "$#00", strlen("$#00"));
}

enum GDBError gdbHandleV(char* commandStart, char *packetEnd) {
    if (strStartsWith(commandStart, "vMustReplyEmpty")) {
        return gdbSendMessage(GDBDataTypeGDB, "$#00", strlen("$#00"));
    } else if (strStartsWith(commandStart, "vCont")) {
        if (commandStart[5] == '?') {
            strcpy(gdbOutputBuffer, "$c;t#");
            return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
        } else {
            OSId threadId;

            char* idLoc = commandStart + 6;
            while (idLoc < packetEnd && *idLoc++ != ':');

            if (idLoc < packetEnd && *idLoc != '#') {
                threadId = gdbParseThreadId(idLoc);
            } else {
                threadId = GDB_ALL_THREADS;
            }

            OSThread *thread = NULL;

            while ((thread = gdbNextThread(thread, threadId))) {
                switch (commandStart[6])
                {
                case 'c':
                {
                    gdbResumeThread(thread);
                    break;
                }
                case 's':
                {
                    // TODO
                    break;
                }
                case 't':
                {
                    osStopThread(thread);
                    break;
                }
                case 'r':
                {
                    // TODO
                    break;
                }
                }
            }

            gdbWaitForStop();
            return GDBErrorNone;
        }
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
            return gdbSendStopReply(GDB_SIGTRAP);
        case 'g':
            return gdbReplyRegisters();
        case 'm':
            return gdbReplyMemory(commandStart, packetEnd);
        case 'D':
            gdbRunFlags &= ~GDB_IS_ATTACHED;
            return gdbSendMessage(GDBDataTypeGDB, "$OK#9a", strlen("$OK#9a"));
        case 'z':
        case 'Z':
        {
            if (commandStart[1] == '0') {
                u32 addr = gdbParseHex(&commandStart[3], 4);

                if (*commandStart == 'z') {
                    gdbRemoveBreakpoint(gdbFindBreakpoint(addr));
                } else {
                    struct GDBBreakpoint* brk = gdbInsertBreakPoint(addr, GDBBreakpointTypeUser);

                    if (!brk) {
                        strcpy(gdbOutputBuffer, "$E00#");
                        return gdbSendMessage(GDBDataTypeGDB, gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
                    }
                }

                return gdbSendMessage(GDBDataTypeGDB, "$OK#9a", strlen("$OK#9a"));
            } else {
                break;
            }
        }
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

void gdbErrorHandler(s16 code, s16 numArgs, ...) {
   va_list valist;

    gdbSendMessage(
        GDBDataTypeText, 
        gdbPacketBuffer, 
        sprintf(gdbPacketBuffer, "code %04X args %04X", code, numArgs)
    );
}

void gdbDebuggerLoop(void *arg) {
    osCreateMesgQueue(&gdbPollMesgQ, &gdbPollMesgQMessage, 1);
    osSetTimer(
        &gdbPollTimer, 
        OS_CPU_COUNTER / GDB_CHECKS_PER_SEC, 
        OS_CPU_COUNTER / GDB_CHECKS_PER_SEC, 
        &gdbPollMesgQ, 
        NULL
    );

    gdbRunFlags |= GDB_IS_ATTACHED;
    while (gdbRunFlags & GDB_IS_ATTACHED) {
        OSMesg msg;
        osRecvMesg(&gdbPollMesgQ, &msg, OS_MESG_BLOCK);
        while (gdbCheckForPacket() == GDBErrorNone);

        if (gdbManualBreak && (gdbRunFlags & GDB_IS_WAITING_STOP)) {
            gdbRunFlags &= ~GDB_IS_WAITING_STOP;
            gdbSendStopReply(GDB_SIGTRAP);
        }

        OSThread* currThread = __osGetCurrFaultedThread();

        while (currThread) {
            if (gdbFindThread(osGetThreadId(currThread)) && (gdbRunFlags & GDB_IS_WAITING_STOP)) {
                gdbSendStopReply(GDB_SIGTRAP);
                gdbRunFlags &= ~GDB_IS_WAITING_STOP;
                break;
            }
            currThread = __osGetNextFaultedThread(currThread);
        }
    }
    osDestroyThread(&gdbDebuggerThread);
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

    osCreateThread(&gdbDebuggerThread, GDB_DEBUGGER_THREAD_ID, gdbDebuggerLoop, NULL, gdbDebuggerThreadStack + GDB_STACKSIZE/sizeof(u64), 11);
    osStartThread(&gdbDebuggerThread);

    if (primaryThread != NULL) {
        osStopThread(primaryThread);
    }
    
    return GDBErrorNone;

}

void gdbBreak() {
    OSThread* currThread = gdbFindThread(osGetThreadId(NULL));

    if (!currThread) {
        currThread = gdbFindThread(GDB_ANY_THREAD);
    }
    
    if (currThread) {
        gdbManualBreak = currThread;
        osStopThread(currThread);
    }
}