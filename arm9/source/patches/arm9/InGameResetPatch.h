#pragma once
#include "../Patch.h"
#include "LoaderInfo.h"
#include "IrqDispatcherVariants.h"

class OSResetSystemPatch;

/// @brief Arm9 patch that returns to the launcher when a key combination is held during
///        gameplay, regardless of whether the game implements a soft reset of its own.
///
///        The keys are sampled in the game's arm9 interrupt dispatcher, and the reset enters
///        the same reboot into Pico Loader that \see OSResetSystemPatch uses. Nearly every
///        retail title uses the stock SDK dispatcher, but a few developers replaced it, so
///        the dispatcher is looked up in a table of known versions
///        (\see irq_dispatcher_variant_t). That reboot is installed here when the
///        game has no OS_ResetSystem, which is common, as the linker drops it from games
///        that never call it. When the dispatcher cannot be found the patch does nothing
///        and the game boots as it would without it.
class InGameResetPatch : public Patch
{
public:
    /// @param osResetSystemPatch The OS_ResetSystem patch, which must be applied before this patch.
    /// @param loaderInfo The loader info to use.
    /// @param twlArm7Sync \c true when the rom is a hybrid rom running in DSi mode, or \c false otherwise.
    /// @param hasSdReadPatchCode \c true when the rom read patches create the platform sd read code,
    ///                           or \c false otherwise.
    InGameResetPatch(const OSResetSystemPatch* osResetSystemPatch, const loader_info_t* loaderInfo,
        bool twlArm7Sync, bool hasSdReadPatchCode)
        : _osResetSystemPatch(osResetSystemPatch), _loaderInfo(loaderInfo)
        , _twlArm7Sync(twlArm7Sync), _hasSdReadPatchCode(hasSdReadPatchCode) { }

    bool FindPatchTarget(PatchContext& patchContext) override;
    void ApplyPatch(PatchContext& patchContext) override;

private:
    const OSResetSystemPatch* _osResetSystemPatch;
    const loader_info_t* _loaderInfo;
    // Pico Loader's arm9 runs from vram, where 8-bit writes have no effect, so these are
    // not bool.
    u16 _twlArm7Sync;
    u16 _hasSdReadPatchCode;
    const irq_dispatcher_variant_t* _irqDispatcherVariant = nullptr;
    IrqDispatcherMatch _irqDispatcherMatch = { };
};
