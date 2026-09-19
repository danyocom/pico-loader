#pragma once
#include "patches/PatchContext.h"

/// @brief The version of the arm9 interrupt dispatcher a game was built with. Nearly every
///        retail game has the stock one; the others are developer replacements, named for
///        what the dispatcher does rather than for the game they were found in, since a
///        second game could share a shape.
enum class IrqDispatcherVariant
{
    /// @brief No known dispatcher was found, and the in-game reset is not installed.
    None,

    /// @brief The stock SDK dispatcher (OS_IrqHandler), used from SDK 2 through SDK 5.
    Sdk,

    /// @brief Picks the interrupt with a single clz and calls the handler with blx.
    ///        Found in Golden Sun: Dark Dawn.
    Blx,

    /// @brief The stock dispatcher wrapped so that interrupts can nest.
    ///        Found in Black Sigil: Blade of the Exiled.
    Nested,

    /// @brief Sets the handler's return address only for some interrupts.
    ///        Found in Diddy Kong Racing DS.
    CondReturn
};

/// @brief Where and how the in-game reset hooks into a game's arm9 interrupt dispatcher,
///        filled in by the search for that dispatcher.
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

/// @brief Finds the one place a dispatcher pattern matches in the ntr region.
///
///        The search only supports a four word pattern, so the first four words are used to
///        find candidates and the rest is compared afterwards. Those four words may appear in
///        unrelated code, and in another dispatcher, so every candidate is compared against
///        the whole pattern rather than only the first. A pattern must therefore begin with
///        four words that are not wildcards.
///
///        Matching in more than one place means it is unclear which is the real dispatcher,
///        which is reported and treated as no match, because patching the wrong one would
///        break the game.
///
/// @param patchContext The patch context to use.
/// @param pattern The instructions that identify the dispatcher.
/// @param patternWordCount The number of words in \p pattern.
/// @param wildcardMask One bit per pattern word, set when that word is allowed to be
///                    anything. Literal pool offsets differ between builds of the same
///                    dispatcher, which is what this is for.
/// @param name The name of the dispatcher version, used in the log.
/// @return The single location the pattern matches, or \c nullptr when it matches no place
///         or more than one.
u32* FindUniqueIrqDispatcherPattern(PatchContext& patchContext, const u32* pattern,
    u32 patternWordCount, u32 wildcardMask, const char* name);

/// @brief Resolves a "ldr Rd, [pc, #imm]" into the word it reads. Reading pc gives the address
///        of the instruction plus 8, so the literal is two words past the load plus its offset.
/// @param patchContext The patch context to use.
/// @param load The instruction to resolve.
/// @param opcode The expected instruction with a zero offset, which is checked so that a
///               wildcarded pattern word is still known to be the right load.
/// @return The word the load reads, or \c nullptr when it is not that instruction or the
///         literal falls outside the ntr region.
const u32* ResolvePcRelativeLoad(PatchContext& patchContext, const u32* load, u32 opcode);

/// @brief Mask covering everything but the offset of a pc relative load.
#define PC_RELATIVE_LOAD_MASK   0xFFFFF000u
