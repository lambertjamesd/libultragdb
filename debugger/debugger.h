
#ifndef __DEBUGGER_H
#define __DEBUGGER_H

#include <ultra64.h>
#include "serial.h"

void gdbInitDebugger(OSPiHandle* handler, OSMesgQueue* dmaMessageQ);

#endif