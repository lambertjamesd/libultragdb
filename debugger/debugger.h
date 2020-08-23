
#ifndef __DEBUGGER_H
#define __DEBUGGER_H

#include <ultra64.h>
#include "serial.h"

enum GDBBreakpointType {
    GDBBreakpointTypeNone,
    GDBBreakpointTypeTemporary,
    GDBBreakpointTypeUser,
    GDBBreakpointTypeUserUnapplied,
};

struct GDBBreakpoint {
    u32 addr;
    u32 prevValue;
    enum GDBBreakpointType type; 
};

#define GDB_MAX_BREAK_POINTS    32

enum GDBError gdbInitDebugger(OSPiHandle* handler, OSMesgQueue* dmaMessageQ, OSThread** forThreads, u32 forThreadsLen);
enum GDBError gdbCheckForPacket();
void gdbBreak();

#endif