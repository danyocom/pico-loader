#include "common.h"
#include "patches/PatchContext.h"
#include "IrqDispatcherNested.h"

// The stock dispatcher wrapped so that interrupts can nest, found in Black Sigil: Blade of
// the Exiled. It is the stock dispatcher up to the table lookup, which is why the pattern has
// to reach past it into the mode switch: without those instructions it would also match the
// stock dispatcher.
static const u32 sPattern[] =
{
    0xE16F0F11u, // clz r0, r1
    0xE1D11033u, // bics r1, r1, r3, lsr r0
    0x1AFFFFFCu, // bne clz
    0xE1A01033u, // lsr r1, r3, r0
    0xE58C1004u, // str r1, [ip, #4]              @ acknowledge
    0xE270001Fu, // rsbs r0, r0, #31
    0xE59F1000u, // ldr r1, [pc, #irqTable]       @ offset differs between builds
    0xE7910100u, // ldr r0, [r1, r0, lsl #2]
    0xE10F3000u, // mrs r3, CPSR                  @ switch to system mode with irqs on
    0xE3C330DFu, // bic r3, r3, #0xDF
    0xE383301Fu, // orr r3, r3, #0x1F
    0xE129F003u, // msr CPSR_fc, r3
    0xE92D4000u, // stmfd sp!, {lr}
    0xE59FE000u, // ldr lr, [pc, #irqReturn]      @ offset differs between builds
    0xE12FFF10u  // bx r0
};

// The two pc relative loads, whose literal pool offsets are the only parts of this dispatcher
// that differ between builds.
#define IRQ_TABLE_LOAD_WORD     6
#define IRQ_TABLE_LOAD_OPCODE   0xE59F1000u // ldr r1, [pc, #imm12], adding the offset
#define IRQ_RETURN_LOAD_WORD    13
#define IRQ_RETURN_LOAD_OPCODE  0xE59FE000u // ldr lr, [pc, #imm12], adding the offset

// The two loads the jump replaces, the mode switch the dispatch part carries on at, and the
// instruction the handler returns to, which is the whole pattern away.
#define HOOK_WORD               6
#define CONTINUE_OFFSET         0x20
#define IRQ_RETURN_OFFSET       0x3C

bool FindNestedIrqDispatcher(PatchContext& patchContext, IrqDispatcherMatch& result)
{
    u32* match = FindUniqueIrqDispatcherPattern(patchContext, sPattern,
        sizeof(sPattern) / sizeof(u32),
        (1u << IRQ_TABLE_LOAD_WORD) | (1u << IRQ_RETURN_LOAD_WORD), "nested");
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

    // The table is an array of 32-bit handler addresses, so its address must be word aligned.
    u32 irqTable = *tableLiteral;
    if (irqTable == 0 || (irqTable & 3) != 0)
    {
        LOG_WARNING("In-game reset: invalid irq table address 0x%X\n", irqTable);
        return false;
    }

    result.hook = match + HOOK_WORD;
    result.irqTable = irqTable;
    result.continueAddress = finalAddress + CONTINUE_OFFSET;
    LOG_DEBUG("In-game reset: nested irq dispatcher at 0x%p (final 0x%X), table 0x%X\n",
        match, finalAddress, irqTable);
    return true;
}
