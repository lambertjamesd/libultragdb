#include "debugger.h"

enum GDBError gdbInitDebugger(OSPiHandle* handler, OSMesgQueue* dmaMessageQ, OSThread** forThreads, u32 forThreadsLen) {
    return GDBErrorNone;
}

enum GDBError gdbCheckForPacket() {
    return GDBErrorNone;
}

void gdbBreak() {}
void* getWatchPoint() {
    return 0;
}
void gdbSetWatchPoint(void* addr, int read, int write) {}
void gdbClearWatchPoint() {}
void gdbHeartbeat() {}