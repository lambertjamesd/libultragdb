
#ifndef __LIBULTRA_GDB_RSP_H
#define __LIBULTRA_GDB_RSP_H

#define RSP_DATA_DUMP_SIZE      0x1000
#define RSP_DUMP_PROGRAM_SIZE   0x1000

#define RSP_WORKING_MEMORY_SIZE (RSP_DUMP_PROGRAM_SIZE + RSP_DATA_DUMP_SIZE)

struct RSPState {
    union {
        struct {
            unsigned int r0, r1, r2, r3, r4, r5, r6, r7;
            unsigned int r8, r9, r10, r11, r12, r13, r14, r15;
            unsigned int r16, r17, r18, r19, r20, r21, r22, r23;
            unsigned int r24, r25, r26, r27, r28, r29, r30, r31;

            unsigned short v0[8], v1[8], v2[8], v3[8];
            unsigned short v4[8], v5[8], v6[8], v7[8];
            unsigned short v8[8], v9[8], v10[8], v11[8];
            unsigned short v12[8], v13[8], v14[8], v15[8];
            unsigned short v16[8], v17[8], v18[8], v19[8];
            unsigned short v20[8], v21[8], v22[8], v23[8];
            unsigned short v24[8], v25[8], v26[8], v27[8];
            unsigned short v28[8], v29[8], v30[8], v31[8];

            unsigned int pc;
            unsigned int reserved;
        };
        unsigned long long _alignment;
    };
};

struct RSPWorkingMemory {
    union {
        struct {
            unsigned char data[RSP_DATA_DUMP_SIZE];
            unsigned char text[RSP_DUMP_PROGRAM_SIZE];
        };
        unsigned long long _alignment;
    };
};

// state must be aligned to 8 bytes
// working memory must be at least RSP_WORKING_MEMORY_SIZE bytes long and aligned to 8 bytes 
void rspDumpState(struct RSPState* state, struct RSPWorkingMemory* workingMemory);

#endif