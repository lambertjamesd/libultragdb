
#ifndef __DEBUGGER_H
#define __DEBUGGER_H

#include <ultra64.h>
#include "serial.h"

enum GDBError gdbInitDebugger(OSPiHandle* handler, OSMesgQueue* dmaMessageQ, OSThread** forThreads, u32 forThreadsLen);
enum GDBError gdbCheckForPacket();
void gdbBreak();

#endif