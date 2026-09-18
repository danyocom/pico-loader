#include "common.h"
#include <array>
#include "patches/PatchContext.h"
#include "InGameResetPatchCode.h"
#include "IrqDispatcherVariants.h"

// The search only supports a four word pattern, so every dispatcher is found by four of its
// words and then compared in full.
#define IRQ_DISPATCHER_ANCHOR_WORDS     4
#define IRQ_DISPATCHER_ANCHOR_SIZE      (IRQ_DISPATCHER_ANCHOR_WORDS * sizeof(u32))

// The stock SDK interrupt dispatcher (OS_IrqHandler). The same code is used from SDK 2
// through SDK 5, on both cpus, and it is what almost every retail game contains.
//
//     ldr r1, =irqTable
//     ldr r0, [r1, r0, lsl #2]
//     ldr lr, =irqReturn
//     bx  r0
//
// The interrupt table and the address the handler returns to follow in the literal pool.
static const u32 sSdkPattern[] =
{
    0xE59F1008u, // ldr r1, =irqTable
    0xE7910100u, // ldr r0, [r1, r0, lsl #2]
    0xE59FE004u, // ldr lr, =irqReturn
    0xE12FFF10u  // bx r0
};

// Start of the same dispatcher: fetching REG_IE, which is not something unrelated code
// ending in the same four instructions would do.
static const u32 sSdkHandlerStart[] =
{
    0xE92D4000u, // push {lr}
    0xE3A0C301u, // mov r12, #0x04000000
    0xE28CCE21u  // add r12, r12, #0x210
};

// How many words before the dispatch to look for the start of the same dispatcher. This
// covers the dispatcher's own length with slack, without reaching far into other code.
#define SDK_HANDLER_MAX_LENGTH_WORDS    32

// Word positions after the four matched instructions: word 4 is the irqTable literal and
// word 5 the irqReturn literal. irqReturn is the instruction right after those two literals,
// so 4 instructions + 2 literals = 6 words = 0x18 bytes past the start of the match.
#define SDK_IRQ_TABLE_WORD              4
#define SDK_IRQ_RETURN_WORD             5
#define SDK_IRQ_RETURN_OFFSET           0x18

// Looks backwards from the dispatch for the dispatcher's first three instructions.
static bool hasSdkHandlerStart(const u32* dispatch, const u32* dataStart)
{
    for (u32 i = 1; i <= SDK_HANDLER_MAX_LENGTH_WORDS; i++)
    {
        const u32* candidate = dispatch - i;
        if (candidate < dataStart)
        {
            break;
        }
        if (candidate[0] == sSdkHandlerStart[0] &&
            candidate[1] == sSdkHandlerStart[1] &&
            candidate[2] == sSdkHandlerStart[2])
        {
            return true;
        }
    }
    return false;
}

static bool validateSdk(PatchContext& patchContext, u32* match, IrqDispatcherMatch& result)
{
    if (!hasSdkHandlerStart(match, patchContext.GetDataStart()))
    {
        LOG_WARNING("In-game reset: irq dispatcher start not found\n");
        return false;
    }

    // The dispatcher normally lives in ITCM and is copied there by autoload when the game
    // starts. Its literal return address is where the instruction after the dispatch ends
    // up, which ties the match to the real dispatcher at its final location.
    u32 finalAddress = patchContext.GetAutoloadAdjuster()->AdjustInitialToFinal((u32)match);
    if (match[SDK_IRQ_RETURN_WORD] != finalAddress + SDK_IRQ_RETURN_OFFSET)
    {
        LOG_WARNING("In-game reset: irq dispatcher return address mismatch\n");
        return false;
    }

    // The table is an array of 32-bit handler addresses, so its address must be word aligned.
    u32 irqTable = match[SDK_IRQ_TABLE_WORD];
    if (irqTable == 0 || (irqTable & 3) != 0)
    {
        LOG_WARNING("In-game reset: invalid irq table address 0x%X\n", irqTable);
        return false;
    }

    result.irqTable = irqTable;
    result.continueAddress = match[SDK_IRQ_RETURN_WORD];
    return true;
}

static u32 getSdkPatchCodeSize()
{
    return InGameResetSdkDispatchPatchCode::GetSize();
}

static const InGameResetDispatchPatchCode* createSdkPatchCode(PatchContext& patchContext,
    const IrqDispatcherMatch& match, const InGameResetKeyCheckPatchCode* keyCheckPatchCode)
{
    return patchContext.GetPatchCodeCollection().AddUniquePatchCode<InGameResetSdkDispatchPatchCode>
    (
        patchContext.GetPatchHeap(),
        match.irqTable,
        match.continueAddress,
        keyCheckPatchCode
    );
}

// Dispatcher versions in the order they are tried. The stock one comes first, because it is
// what nearly every game has.
static const std::array<const irq_dispatcher_variant_t, 1> sIrqDispatcherVariants
{
    irq_dispatcher_variant_t
    {
        .name = "sdk",
        .pattern = sSdkPattern,
        .patternWordCount = sizeof(sSdkPattern) / sizeof(u32),
        .wildcardMask = 0,
        .anchorWordIndex = 0,
        .hookWordIndex = 0,
        .validate = validateSdk,
        .getPatchCodeSize = getSdkPatchCodeSize,
        .createPatchCode = createSdkPatchCode
    }
};

// Compares a candidate against the whole pattern, skipping the wildcard words.
static bool matchesPattern(const u32* candidate, const u32* dataStart, const u32* dataEnd,
    const irq_dispatcher_variant_t& variant)
{
    if (candidate < dataStart || candidate + variant.patternWordCount > dataEnd)
    {
        return false;
    }
    for (u32 i = 0; i < variant.patternWordCount; i++)
    {
        if (!(variant.wildcardMask & (1u << i)) && candidate[i] != variant.pattern[i])
        {
            return false;
        }
    }
    return true;
}

const irq_dispatcher_variant_t* FindIrqDispatcher(PatchContext& patchContext, IrqDispatcherMatch& result)
{
    auto autoloadAdjuster = patchContext.GetAutoloadAdjuster();
    if (!autoloadAdjuster)
    {
        LOG_WARNING("In-game reset: no autoload information\n");
        return nullptr;
    }

    const u32* dataStart = patchContext.GetDataStart();
    const u32* dataEnd = patchContext.GetDataEnd();

    for (const auto& variant : sIrqDispatcherVariants)
    {
        const u32* anchor = variant.pattern + variant.anchorWordIndex;

        // The anchor can appear in unrelated code, so every hit is compared against the
        // whole pattern rather than only the first.
        u32* match = nullptr;
        u32 matchCount = 0;
        for (u32* hit = patchContext.FindPattern32(anchor, IRQ_DISPATCHER_ANCHOR_SIZE);
             hit != nullptr;
             hit = patchContext.FindPattern32(anchor, IRQ_DISPATCHER_ANCHOR_SIZE, hit + 1))
        {
            u32* candidate = hit - variant.anchorWordIndex;
            if (!matchesPattern(candidate, dataStart, dataEnd, variant))
            {
                continue;
            }
            if (matchCount++ == 0)
            {
                match = candidate;
            }
        }

        if (matchCount == 0)
        {
            continue;
        }

        // A second match means it is unclear which one is the real dispatcher, so patch neither.
        if (matchCount > 1)
        {
            LOG_WARNING("In-game reset: %s irq dispatcher is ambiguous\n", variant.name);
            continue;
        }

        result = { };
        result.hook = match + variant.hookWordIndex;
        if (!variant.validate(patchContext, match, result))
        {
            continue;
        }

        LOG_DEBUG("In-game reset: %s irq dispatcher at 0x%p (final 0x%X), table 0x%X\n",
            variant.name, match, autoloadAdjuster->AdjustInitialToFinal((u32)match), result.irqTable);
        return &variant;
    }

    LOG_DEBUG("In-game reset: no known irq dispatcher found\n");
    return nullptr;
}
