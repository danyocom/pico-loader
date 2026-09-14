#pragma once
#include "../Patch.h"
#include "LoaderInfo.h"

class OSResetSystemPatchCode;

/// @brief Arm9 patch to make OS_ResetSystem reboot into Pico Loader.
class OSResetSystemPatch : public Patch
{
public:
    OSResetSystemPatch(const loader_info_t* loaderInfo, bool runInDSiMode)
        : _runInDSiMode(runInDSiMode), _loaderInfo(loaderInfo) { }

    bool FindPatchTarget(PatchContext& patchContext) override;
    void ApplyPatch(PatchContext& patchContext) override;

    void** GetCheatsPointerAtTarget() const
    {
        return _cheatsPointer;
    }

    /// @brief Returns whether OS_ResetSystem was found and patched. Only valid after \see ApplyPatch.
    bool IsApplied() const
    {
        // ApplyPatch sets the cheats pointer only after it has written the jump into the reboot.
        return _cheatsPointer != nullptr;
    }

    /// @brief Returns the patch code that reboots into Pico Loader, creating it when this is
    ///        the first request. The reboot does not depend on the game containing OS_ResetSystem,
    ///        so other patches can enter it too.
    /// @param patchContext The patch context to use.
    /// @param loaderInfo The loader info to use.
    /// @param twlArm7Sync \c true when the arm7 of a hybrid rom running in DSi mode must be synced
    ///                    through shared memory before the reset, or \c false otherwise.
    ///                    Only used when the patch code is created.
    /// @return The reboot patch code.
    static const OSResetSystemPatchCode* GetOrAddRebootPatchCode(
        PatchContext& patchContext, const loader_info_t* loaderInfo, bool twlArm7Sync);

    /// @brief Number of allocations for the reboot: the loader info copy and the reboot's two code parts.
    static constexpr u32 REBOOT_PATCH_CODE_ALLOCATION_COUNT = 3;

    /// @brief Gets the sizes of the patch heap allocations made when creating the reboot patch code,
    ///        in allocation order, besides the platform sd read code that the rom read patches also use.
    /// @param sizes Receives \see REBOOT_PATCH_CODE_ALLOCATION_COUNT sizes.
    /// @return The number of sizes written.
    static u32 GetRebootPatchCodeAllocationSizes(u32* sizes);

private:
    u32* _osResetSystem = nullptr;
    // Pico Loader's arm9 runs from vram, where 8-bit writes have no effect, so these are not bool.
    u16 _hybrid = false;
    u16 _runInDSiMode;
    const loader_info_t* _loaderInfo;
    void** _cheatsPointer = nullptr;
};
