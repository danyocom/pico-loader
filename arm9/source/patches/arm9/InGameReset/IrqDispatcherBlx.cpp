#include "common.h"
#include "patches/PatchContext.h"
#include "IrqDispatcherBlx.h"

// A dispatcher that picks the interrupt with a single clz and calls the handler with blx,
// found in Golden Sun: Dark Dawn. Everything after the IME test is conditional, so the whole
// run from the dispatcher's first instruction to its call is one pattern, which identifies it
// far more tightly than the stock dispatcher's four instructions do.
static const u32 sPattern[] =
{
    0xE3A0C301u, // mov ip, #0x04000000
    0xE5BC2208u, // ldr r2, [ip, #0x208]!        @ IME
    0xE1EC00D8u, // ldrd r0, r1, [ip, #8]!       @ IE, IF
    0xE3520000u, // cmp r2, #0
    0x10101001u, // andsne r1, r0, r1
    0x152DE004u, // pushne {lr}
    0x159FE000u, // ldrne lr, [pc, #irqTableEnd] @ offset differs between builds
    0x12610000u, // rsbne r0, r1, #0
    0x10001001u, // andne r1, r0, r1
    0x116F0F11u, // clzne r0, r1
    0x171EE100u, // ldrne lr, [lr, -r0, lsl #2]
    0x158C1004u, // strne r1, [ip, #4]
    0xE12FFF3Eu  // blx lr
};

// The load of the table address, whose literal pool offset is the only part of this
// dispatcher that differs between builds.
#define IRQ_TABLE_LOAD_WORD     6
#define IRQ_TABLE_LOAD_OPCODE   0x159FE000u // ldrne lr, [pc, #imm12], adding the offset

// The two conditional instructions the jump replaces, and the address of the instruction
// after the call, which is where the handler returns to. 13 words = 0x34 bytes.
#define HOOK_WORD               10
#define IRQ_RETURN_OFFSET       0x34

// SDK code that runs from instruction tightly-coupled memory is linked into this range, which
// is a mirror of ITCM. A dispatcher that autoload does not place there is not this one.
#define ITCM_MIRROR_START       0x01FF8000u
#define ITCM_MIRROR_END         0x02000000u

bool FindBlxIrqDispatcher(PatchContext& patchContext, IrqDispatcherMatch& result)
{
    u32* match = FindUniqueIrqDispatcherPattern(patchContext, sPattern,
        sizeof(sPattern) / sizeof(u32), 1u << IRQ_TABLE_LOAD_WORD, "blx");
    if (!match)
    {
        return false;
    }

    // This dispatcher has no literal holding the address it runs from, so instead check that
    // autoload does move it into ITCM, which is where it has to run.
    u32 finalAddress = patchContext.GetAutoloadAdjuster()->AdjustInitialToFinal((u32)match);
    if (finalAddress < ITCM_MIRROR_START || finalAddress >= ITCM_MIRROR_END)
    {
        LOG_WARNING("In-game reset: irq dispatcher is not in itcm (0x%X)\n", finalAddress);
        return false;
    }

    // The dispatch part does not need the table, because the dispatcher has already loaded
    // it, but a match whose table is nonsense is not this dispatcher. Resolving the load also
    // confirms the wildcarded word is still the pc relative load it should be.
    const u32* tableLiteral = ResolvePcRelativeLoad(patchContext,
        &match[IRQ_TABLE_LOAD_WORD], IRQ_TABLE_LOAD_OPCODE);
    if (!tableLiteral)
    {
        LOG_WARNING("In-game reset: irq table load mismatch\n");
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
    result.continueAddress = finalAddress + IRQ_RETURN_OFFSET;
    LOG_DEBUG("In-game reset: blx irq dispatcher at 0x%p (final 0x%X), table 0x%X\n",
        match, finalAddress, irqTable);
    return true;
}
