#include "common.h"
#include "patches/PatchContext.h"
#include "IrqDispatcher.h"

// The first four words of a pattern are what the search looks for, because that is the only
// width it supports.
#define IRQ_DISPATCHER_ANCHOR_SIZE  (4 * sizeof(u32))

// Compares a candidate against the whole pattern, skipping the wildcard words.
static bool matchesPattern(const u32* candidate, const u32* pattern, u32 patternWordCount,
    u32 wildcardMask, const u32* dataEnd)
{
    if (candidate + patternWordCount > dataEnd)
    {
        return false;
    }
    for (u32 i = 0; i < patternWordCount; i++)
    {
        if (!(wildcardMask & (1u << i)) && candidate[i] != pattern[i])
        {
            return false;
        }
    }
    return true;
}

u32* FindUniqueIrqDispatcherPattern(PatchContext& patchContext, const u32* pattern,
    u32 patternWordCount, u32 wildcardMask, const char* name)
{
    const u32* dataEnd = patchContext.GetDataEnd();

    u32* match = nullptr;
    u32 matchCount = 0;
    for (u32* hit = patchContext.FindPattern32(pattern, IRQ_DISPATCHER_ANCHOR_SIZE);
         hit != nullptr;
         hit = patchContext.FindPattern32(pattern, IRQ_DISPATCHER_ANCHOR_SIZE, hit + 1))
    {
        if (!matchesPattern(hit, pattern, patternWordCount, wildcardMask, dataEnd))
        {
            continue;
        }
        if (matchCount++ == 0)
        {
            match = hit;
        }
    }

    if (matchCount > 1)
    {
        LOG_WARNING("In-game reset: %s irq dispatcher is ambiguous\n", name);
        return nullptr;
    }
    return match;
}

const u32* ResolvePcRelativeLoad(PatchContext& patchContext, const u32* load, u32 opcode)
{
    if ((*load & PC_RELATIVE_LOAD_MASK) != opcode)
    {
        return nullptr;
    }
    const u32* literal = load + 2 + ((*load & ~PC_RELATIVE_LOAD_MASK) >> 2);
    return literal < patchContext.GetDataEnd() ? literal : nullptr;
}
