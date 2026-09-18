#include "common.h"
#include "patches/PatchContext.h"
#include "IrqDispatcherSdk.h"

// The stock SDK interrupt dispatcher (OS_IrqHandler). The same code is used from SDK 2
// through SDK 5, on both cpus, and it is what almost every retail game contains.
//
//     ldr r1, =irqTable
//     ldr r0, [r1, r0, lsl #2]
//     ldr lr, =irqReturn
//     bx  r0
//
// The interrupt table and the address the handler returns to follow in the literal pool.
static const u32 sPattern[] =
{
    0xE59F1008u, // ldr r1, =irqTable
    0xE7910100u, // ldr r0, [r1, r0, lsl #2]
    0xE59FE004u, // ldr lr, =irqReturn
    0xE12FFF10u  // bx r0
};

// Start of the same dispatcher: fetching REG_IE, which is not something unrelated code
// ending in the same four instructions would do. Four instructions is short enough that this
// extra check is worth making; the other dispatchers have patterns long enough not to need one.
static const u32 sHandlerStart[] =
{
    0xE92D4000u, // push {lr}
    0xE3A0C301u, // mov r12, #0x04000000
    0xE28CCE21u  // add r12, r12, #0x210
};

// How many words before the dispatch to look for the start of the same dispatcher. This
// covers the dispatcher's own length with slack, without reaching far into other code.
#define HANDLER_MAX_LENGTH_WORDS    32

// Word positions after the four matched instructions: word 4 is the irqTable literal and
// word 5 the irqReturn literal. irqReturn is the instruction right after those two literals,
// so 4 instructions + 2 literals = 6 words = 0x18 bytes past the start of the match.
#define IRQ_TABLE_WORD              4
#define IRQ_RETURN_WORD             5
#define IRQ_RETURN_OFFSET           0x18

// The jump replaces the whole four instruction dispatch.
#define HOOK_WORD                   0

// Looks backwards from the dispatch for the dispatcher's first three instructions.
static bool hasHandlerStart(const u32* dispatch, const u32* dataStart)
{
    for (u32 i = 1; i <= HANDLER_MAX_LENGTH_WORDS; i++)
    {
        const u32* candidate = dispatch - i;
        if (candidate < dataStart)
        {
            break;
        }
        if (candidate[0] == sHandlerStart[0] &&
            candidate[1] == sHandlerStart[1] &&
            candidate[2] == sHandlerStart[2])
        {
            return true;
        }
    }
    return false;
}

bool FindSdkIrqDispatcher(PatchContext& patchContext, IrqDispatcherMatch& result)
{
    u32* match = FindUniqueIrqDispatcherPattern(patchContext, sPattern,
        sizeof(sPattern) / sizeof(u32), 0, "sdk");
    if (!match)
    {
        return false;
    }

    if (!hasHandlerStart(match, patchContext.GetDataStart()))
    {
        LOG_WARNING("In-game reset: irq dispatcher start not found\n");
        return false;
    }

    // The dispatcher normally lives in ITCM and is copied there by autoload when the game
    // starts. Its literal return address is where the instruction after the dispatch ends
    // up, which ties the match to the real dispatcher at its final location.
    u32 finalAddress = patchContext.GetAutoloadAdjuster()->AdjustInitialToFinal((u32)match);
    if (match[IRQ_RETURN_WORD] != finalAddress + IRQ_RETURN_OFFSET)
    {
        LOG_WARNING("In-game reset: irq dispatcher return address mismatch\n");
        return false;
    }

    // The table is an array of 32-bit handler addresses, so its address must be word aligned.
    u32 irqTable = match[IRQ_TABLE_WORD];
    if (irqTable == 0 || (irqTable & 3) != 0)
    {
        LOG_WARNING("In-game reset: invalid irq table address 0x%X\n", irqTable);
        return false;
    }

    result.hook = match + HOOK_WORD;
    result.irqTable = irqTable;
    result.continueAddress = match[IRQ_RETURN_WORD];
    LOG_DEBUG("In-game reset: sdk irq dispatcher at 0x%p (final 0x%X), table 0x%X\n",
        match, finalAddress, irqTable);
    return true;
}
