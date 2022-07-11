
#include <ultra64.h>
#include "./rsp.h"

extern char dump_rsp_stateTextStart[];
extern char dump_rsp_stateTextEnd[];

int rspDMABusy()
{
    return IO_READ(SP_STATUS_REG) & (SP_STATUS_DMA_BUSY | SP_STATUS_DMA_FULL | SP_STATUS_IO_FULL);
}

void rspRamToDMEM(u32 rspAddr, void* srcMemory, u32 len)
{
    IO_WRITE(SP_MEM_ADDR_REG, rspAddr);
    IO_WRITE(SP_DRAM_ADDR_REG, osVirtualToPhysical(srcMemory));
    IO_WRITE(SP_RD_LEN_REG, len - 1);
    while (rspDMABusy());
}

void rspDMEMtoRam(u32 rspAddr, void* dstMemory, u32 len) 
{
    IO_WRITE(SP_MEM_ADDR_REG, rspAddr);
    IO_WRITE(SP_DRAM_ADDR_REG, osVirtualToPhysical(dstMemory));
    IO_WRITE(SP_WR_LEN_REG, len - 1);
    while (rspDMABusy());
}

int rspIsBranch(u32 instruction)
{
    u32 opcode = (instruction & 0xFC000000) >> 16;
    u32 regimm = (instruction & 0x1F0000) >> 16;

    if (
        // BEQ BNE BLEZ BGTZ
        (opcode & 0xF0FF) == 0x1000 ||
        // BGEZ BGEZAL BLTZ BLTZAL
        opcode == 0x0400 && (
            regimm == 0x01 || 
            regimm == 0x11 ||
            regimm == 0x00 ||
            regimm == 0x10) &&
        opcode == 0x0000 && (
            (instruction & 0x3F) == 0x09 ||
            (instruction & 0x3F) == 0x08
        ) &&
        // J
        opcode == 0x0800 ||
        // JAL
        opcode == 0x0C00) {
        return 1;
    } else {
        return 0;
    }
}

u32 rspAdjustPCForRestart(u32 pc)
{
    if (pc == 0)
    {
        return pc;
    }

    u32 prevInstruction = IO_READ(SP_IMEM_START + pc - 4);

    if (rspIsBranch(prevInstruction))
    {
        return pc - 4;
    }
    else
    {
        return pc;
    }
}

void rspDumpState(struct RSPState* state, struct RSPWorkingMemory* workingMemory)
{
    OSIntMask prevMask = osGetIntMask();
    // disable interrupts
    osSetIntMask(0);

    while (IO_READ(SP_SEMAPHORE_REG));

    // stop rsp
    int prevStatus = IO_READ(SP_STATUS_REG);

    if (!(prevStatus & SP_STATUS_HALT)) {
        IO_WRITE(SP_STATUS_REG, SP_SET_HALT);
        while (!(IO_READ(SP_STATUS_REG) & SP_STATUS_HALT));
    }

    u32 prevPC = IO_READ(SP_PC_REG);

    while (rspDMABusy());

    // save program and data
    rspDMEMtoRam(SP_DMEM_START, workingMemory->data, RSP_DATA_DUMP_SIZE);
    rspDMEMtoRam(SP_IMEM_START, workingMemory->text, RSP_DUMP_PROGRAM_SIZE);

    // load and run state dumper
    rspRamToDMEM(SP_IMEM_START, dump_rsp_stateTextStart, dump_rsp_stateTextEnd - dump_rsp_stateTextStart);
    IO_WRITE(SP_PC_REG, 0);
	IO_WRITE(SP_STATUS_REG, SP_CLR_INTR_BREAK | SP_CLR_SSTEP | SP_CLR_BROKE | SP_CLR_HALT);

    // wait for program to finish
    while (!(IO_READ(SP_STATUS_REG) & SP_STATUS_BROKE));

    // extract dumped state
    rspDMEMtoRam(SP_DMEM_START, state, sizeof(struct RSPState));
    state->pc = prevPC;

    // restore program and data
    rspRamToDMEM(SP_DMEM_START, workingMemory->data, RSP_DATA_DUMP_SIZE);
    rspRamToDMEM(SP_IMEM_START, workingMemory->text, RSP_DUMP_PROGRAM_SIZE);
    IO_WRITE(SP_PC_REG, rspAdjustPCForRestart(prevPC));

    u32 nextFlags = 0;

    // optionally restart rsp
    if (!(prevStatus & SP_STATUS_HALT)) {
        nextFlags |= SP_CLR_HALT;
    }

    if (!(prevStatus & SP_STATUS_BROKE)) {
        nextFlags |= SP_CLR_BROKE;
    }

    if (prevStatus & SP_STATUS_INTR_BREAK) {
        nextFlags |= SP_SET_INTR_BREAK;
    }

    if (prevStatus & SP_STATUS_SSTEP) {
        nextFlags |= SP_SET_SSTEP;
    }

    if (nextFlags) {
        IO_WRITE(SP_STATUS_REG, nextFlags);
    }

    IO_WRITE(SP_SEMAPHORE_REG, 0);

    // restore interrupts
    osSetIntMask(prevMask);
}