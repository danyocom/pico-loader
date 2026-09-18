#pragma once
#include "IAutoloadAdjuster.h"
#include "patches/PatchContext.h"

class InGameResetDispatchPatchCode;
class InGameResetKeyCheckPatchCode;

/// @brief Where and how the in-game reset hooks into a game's arm9 interrupt dispatcher,
///        filled in by \see irq_dispatcher_variant_t::validate.
struct IrqDispatcherMatch
{
    /// @brief The two words of the dispatcher that are overwritten with a jump to the
    ///        dispatch part.
    u32* hook;

    /// @brief The address, after autoload has moved the dispatcher, at which the dispatch
    ///        part carries on with the game's own code. What it is used for depends on the
    ///        dispatcher: it may be the address the handler returns to, or the instruction
    ///        the dispatch part resumes at.
    u32 continueAddress;

    /// @brief The address of the game's interrupt handler table, or 0 for a dispatcher that
    ///        does not need it because it already has it in a register.
    u32 irqTable;
};

/// @brief One version of the arm9 interrupt dispatcher, describing how to recognise it and
///        how to hook it. Everything after the dispatch is shared, so supporting a newly
///        found dispatcher means adding a dispatch part and one entry to the table.
struct irq_dispatcher_variant_t
{
    /// @brief Name of this dispatcher version, used in the log.
    const char* name;

    /// @brief The instructions that identify this dispatcher.
    const u32* pattern;

    /// @brief The number of words in \see pattern.
    u32 patternWordCount;

    /// @brief One bit per pattern word, set when that word is allowed to be anything.
    ///        Literal pool offsets differ between builds of the same dispatcher.
    u32 wildcardMask;

    /// @brief The index of the four pattern words that are searched for. The search only
    ///        supports a four word pattern, so the rest is compared afterwards. These four
    ///        words may match unrelated code; what identifies the dispatcher is the whole
    ///        pattern.
    u32 anchorWordIndex;

    /// @brief The index of the first of the two pattern words that the jump overwrites.
    u32 hookWordIndex;

    /// @brief Performs the checks that the pattern alone cannot make, such as comparing a
    ///        literal against the address the dispatcher ends up at after autoload, and
    ///        fills in \p result. Returning false rejects the match.
    bool (*validate)(PatchContext& patchContext, u32* match, IrqDispatcherMatch& result);

    /// @brief Returns the patch heap space this version's dispatch part needs.
    u32 (*getPatchCodeSize)();

    /// @brief Creates this version's dispatch part.
    const InGameResetDispatchPatchCode* (*createPatchCode)(PatchContext& patchContext,
        const IrqDispatcherMatch& match, const InGameResetKeyCheckPatchCode* keyCheckPatchCode);
};

/// @brief Searches the ntr region for an arm9 interrupt dispatcher that the in-game reset
///        knows how to hook.
/// @param patchContext The patch context to use.
/// @param result Receives the hook location and addresses when a dispatcher is found.
/// @return The dispatcher version that was found, or \c nullptr when none was.
const irq_dispatcher_variant_t* FindIrqDispatcher(PatchContext& patchContext, IrqDispatcherMatch& result);
