
#include "debugger.h"
#include <string.h>

#define MAX_PACKET_SIZE     0x4000

// +1 to ensure there is always a null terminator in this string
// extra GDB_USB_SERIAL_SIZE to make sure packets sent back to 
// back can be parsed properly
static char gdbPacketBuffer[MAX_PACKET_SIZE + GDB_USB_SERIAL_SIZE + 1];
static char gdbOutputBuffer[MAX_PACKET_SIZE];
static int gdbNextBufferTarget;
static int gdbNextSearchIndex;

void println(char* text);

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
    }

    sprintf(message, "%02x", checksum);

    return (message - messageStart) + 2;
}

enum GDBError gdbReadPacket(char **commandStart, char **commandEnd, char **packetEnd)
{
    if (gdbNextBufferTarget > 0)
    {
        strncpy(gdbPacketBuffer, gdbPacketBuffer + gdbNextBufferTarget - GDB_USB_SERIAL_SIZE, GDB_USB_SERIAL_SIZE);
        gdbNextSearchIndex -= gdbNextBufferTarget - GDB_USB_SERIAL_SIZE;
        gdbNextBufferTarget = 0;
    }

    while (gdbNextBufferTarget < MAX_PACKET_SIZE + GDB_USB_SERIAL_SIZE) {
        if (gdbNextSearchIndex == gdbNextBufferTarget) {
            // TODO handle partial packets
            
            enum GDBError readResult = gdbSerialRead(&gdbPacketBuffer[gdbNextBufferTarget], GDB_USB_SERIAL_SIZE);

            if (readResult != GDBErrorNone) {
                return readResult;
            }

            gdbNextBufferTarget += GDB_USB_SERIAL_SIZE;
        }

        while (gdbNextSearchIndex < gdbNextBufferTarget)
        {
            if (gdbPacketBuffer[gdbNextSearchIndex] == '$') {
                ++gdbNextSearchIndex;
                *commandStart = &gdbPacketBuffer[gdbNextSearchIndex];
                break;
            }
            ++gdbNextSearchIndex;
        }
        
        while (gdbPacketBuffer[gdbNextSearchIndex])
        {
            if (!gdbIsAlphaNum(gdbPacketBuffer[gdbNextSearchIndex]))
            {
                *commandEnd = &gdbPacketBuffer[gdbNextSearchIndex];
                break;
            }
            else
            {
                ++gdbNextSearchIndex;
            }
        }

        while (gdbPacketBuffer[gdbNextSearchIndex]) {
            if (gdbPacketBuffer[gdbNextSearchIndex] == '#') {
                *packetEnd = &gdbPacketBuffer[gdbNextSearchIndex];
                ++gdbNextSearchIndex;
                return GDBErrorNone;
            }
            else
            {
                ++gdbNextSearchIndex;
            }
        } 
    }

    return GDBErrorBadPacket;
}

enum GDBError gdbHandleQuery(char* commandStart, char* commandEnd, char *packetEnd) {
    if (strncmp(commandStart, "qSupported", strlen("qSupported")) == 0) {
        strcpy(gdbOutputBuffer, "$PacketSize=4000;vContSupported+#");
        return gdbSerialWrite(gdbOutputBuffer, gdbApplyChecksum(gdbOutputBuffer));
    }

    return gdbSerialWrite("$#00", strlen("$#00"));
}

enum GDBError gdbHandleV(char* commandStart, char* commandEnd, char *packetEnd) {
    if (strncmp(commandEnd, "vMustReplyEmpty", strlen("vMustReplyEmpty")) == 0) {
        return gdbSerialWrite("$#00", strlen("$#00"));
    }

    return gdbSerialWrite("$#00", strlen("$#00"));
}

enum GDBError gdbHandlePacket(char* commandStart, char* commandEnd, char *packetEnd) {
    switch (*commandStart) {
        case 'q':
            return gdbHandleQuery(commandStart, commandEnd, packetEnd);
        case 'v':
            return gdbHandleV(commandStart, commandEnd, packetEnd);
    }

    return gdbSerialWrite("$#00", strlen("$#00"));
}

enum GDBError gdbCheckForPacket() {
    char* commandStart;
    char* commandEnd;
    char* packetEnd;

    if (gdbSerialCanRead()) {
        enum GDBError err = gdbReadPacket(&commandStart, &commandEnd, &packetEnd);

        if (err != GDBErrorNone) {
            return err;
        }

        err = gdbSerialWrite("+", strlen("+"));

        if (err != GDBErrorNone) {
            return err;
        }

        err = gdbHandlePacket(commandStart, commandEnd, packetEnd);

        if (err != GDBErrorNone) {
            return err;
        }
    }

    return GDBErrorNone;
}

enum GDBError gdbInitDebugger(OSPiHandle* handler, OSMesgQueue* dmaMessageQ)
{
    enum GDBError err = gdbSerialInit(handler, dmaMessageQ);
    if (err != GDBErrorNone) return err;

    // gdbCheckForPacket();
    
    return GDBErrorNone;

}