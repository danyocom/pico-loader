#include "common.h"
#include "patches/PatchContext.h"
#include "IrqDispatcherCondReturn.h"

// A dispatcher that sets the handler's return address only for some interrupts, found in
// Diddy Kong Racing DS. It is the stock dispatcher up to the acknowledge, so the pattern has
// to reach past it into the dispatch, where it loads the handler into r2 instead of r0 and
// makes the push and the return address conditional.
static const u32 sPattern[] =
{
    0xE16F0F11u, // clz r0, r1
    0xE1D11033u, // bics r1, r1, r3, lsr r0
    0x1AFFFFFCu, // bne clz
    0xE1A01033u, // lsr r1, r3, r0
    0xE58C1004u, // str r1, [ip, #4]              @ acknowledge
    0xE59F1000u, // ldr r1, [pc, #irqTable]       @ offset differs between builds
    0xE270001Fu, // rsbs r0, r0, #31
    0xE7912100u, // ldr r2, [r1, r0, lsl #2]      @ handler in r2, not r0
    0xE350000Au, // cmp r0, #10
    0x192D4000u, // stmfdne sp!, {lr}
    0x159FE000u, // ldrne lr, [pc, #irqReturn]    @ offset differs between builds
    0xE12FFF12u  // bx r2
};

// The two pc relative loads, whose literal pool offsets are the only parts of this dispatcher
// that differ between builds.
#define IRQ_TABLE_LOAD_WORD     5
#define IRQ_TABLE_LOAD_OPCODE   0xE59F1000u // ldr r1, [pc, #imm12], adding the offset
#define IRQ_RETURN_LOAD_WORD    10
#define IRQ_RETURN_LOAD_OPCODE  0x159FE000u // ldrne lr, [pc, #imm12], adding the offset

// The conditional return address load and the call the jump replaces, and the instruction the
// handler returns to, which is the whole pattern plus its two literals away.
#define HOOK_WORD               10
#define IRQ_RETURN_OFFSET       0x38

bool FindCondReturnIrqDispatcher(PatchContext& patchContext, IrqDispatcherMatch& result)
{
    u32* match = FindUniqueIrqDispatcherPattern(patchContext, sPattern,
        sizeof(sPattern) / sizeof(u32),
        (1u << IRQ_TABLE_LOAD_WORD) | (1u << IRQ_RETURN_LOAD_WORD), "condreturn");
    if (!match)
    {
        return false;
    }

    // Resolving the loads also confirms the wildcarded words are still the pc relative loads
    // they should be.
    const u32* tableLiteral = ResolvePcRelativeLoad(patchContext,
        &match[IRQ_TABLE_LOAD_WORD], IRQ_TABLE_LOAD_OPCODE);
    const u32* returnLiteral = ResolvePcRelativeLoad(patchContext,
        &match[IRQ_RETURN_LOAD_WORD], IRQ_RETURN_LOAD_OPCODE);
    if (!tableLiteral || !returnLiteral)
    {
        LOG_WARNING("In-game reset: irq dispatcher literal loads mismatch\n");
        return false;
    }

    // The literal return address is where the instruction after the dispatcher's call ends
    // up, which ties the match to the real dispatcher at its final location.
    u32 finalAddress = patchContext.GetAutoloadAdjuster()->AdjustInitialToFinal((u32)match);
    if (*returnLiteral != finalAddress + IRQ_RETURN_OFFSET)
    {
        LOG_WARNING("In-game reset: irq dispatcher return address mismatch\n");
        return false;
    }

    // The dispatch part does not need the table, because the dispatcher has already looked the
    // handler up, but a match whose table is nonsense is not this dispatcher. The table is an
    // array of 32-bit handler addresses, so its address must be word aligned.
    u32 irqTable = *tableLiteral;
    if (irqTable == 0 || (irqTable & 3) != 0)
    {
        LOG_WARNING("In-game reset: invalid irq table address 0x%X\n", irqTable);
        return false;
    }

    result.hook = match + HOOK_WORD;
    result.irqTable = irqTable;
    result.continueAddress = *returnLiteral;
    LOG_DEBUG("In-game reset: condreturn irq dispatcher at 0x%p (final 0x%X), table 0x%X\n",
        match, finalAddress, irqTable);
    return true;
}
