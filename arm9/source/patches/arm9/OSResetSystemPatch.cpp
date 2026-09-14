#include "common.h"
#include "patches/PatchContext.h"
#include "thumbInstructions.h"
#include "patches/platform/LoaderPlatform.h"
#include "OSResetSystemPatchCode.h"
#include "OSResetSystemPatch.h"

static const u32 sOSResetSystemPatternSdk2Old[] = { 0xE59F101Cu, 0xE3A00010u, 0xE5815000u, 0xEB000005u };
static const u32 sOSResetSystemPatternSdk2[] = { 0xE59F1018u, 0xE3A00010u, 0xE5814000u, 0xEB000004u };
static const u32 sOSResetSystemPatternSdk2New[] = { 0xE59F1014u, 0xE3A00010u, 0xE5814000u, 0xEB000003u };
static const u32 sOSResetSystemPatternSdk3[] = { 0xE59F1014u, 0xE3A00010u, 0xE5814000u, 0xEBFFFFD9u };
static const u32 sOSResetSystemPatternSdk4[] = { 0xE59F103Cu, 0xE59F003Cu, 0xE5814000u, 0xEBFFFFD6u };
static const u32 sOSResetSystemPatternPokemonDownloader[] = { 0xE59F1010u, 0xE3A00010u, 0xE5814000u, 0xEBFFFFDEu };

static const u32 sOSResetSystemPatternSdk5Old[] = { 0xE1A04000u, 0xE1D100B0u, 0xE3500002u, 0x1A000000u };
static const u32 sOSResetSystemPatternSdk5New[] = { 0xE1A05000u, 0xE1D100B0u, 0xE3500002u, 0x1A000000u };
static const u32 sOSResetSystemPatternSdk5HybridOld[] = { 0xE1A04000u, 0xE1D100B0u, 0xE3500002u, 0x0A000006 };
static const u32 sOSResetSystemPatternSdk5HybridNew[] = { 0xE1A05000u, 0xE1D100B0u, 0xE3500002u, 0x0A000006 };

bool OSResetSystemPatch::FindPatchTarget(PatchContext& patchContext)
{
    _osResetSystem = nullptr;
    if (patchContext.GetSdkVersion().IsTwlSdk())
    {
        _osResetSystem = patchContext.FindPattern32(sOSResetSystemPatternSdk5Old, sizeof(sOSResetSystemPatternSdk5Old));
        if (!_osResetSystem)
        {
            _osResetSystem = patchContext.FindPattern32(sOSResetSystemPatternSdk5New, sizeof(sOSResetSystemPatternSdk5New));
        }
        if (!_osResetSystem)
        {
            _osResetSystem = patchContext.FindPattern32(sOSResetSystemPatternSdk5HybridOld, sizeof(sOSResetSystemPatternSdk5HybridOld));
            _hybrid = true;
        }
        if (!_osResetSystem)
        {
            _osResetSystem = patchContext.FindPattern32(sOSResetSystemPatternSdk5HybridNew, sizeof(sOSResetSystemPatternSdk5HybridNew));
            _hybrid = true;
        }
    }
    else
    {
        if (patchContext.GetSdkVersion() >= 0x4017530)
        {
            _osResetSystem = patchContext.FindPattern32(sOSResetSystemPatternSdk4, sizeof(sOSResetSystemPatternSdk4));
        }
        if (!_osResetSystem && patchContext.GetSdkVersion() >= 0x3017530)
        {
            _osResetSystem = patchContext.FindPattern32(sOSResetSystemPatternSdk3, sizeof(sOSResetSystemPatternSdk3));
        }
        if (!_osResetSystem && patchContext.GetSdkVersion() >= 0x2017532)
        {
            _osResetSystem = patchContext.FindPattern32(sOSResetSystemPatternSdk2New, sizeof(sOSResetSystemPatternSdk2New));
        }
        if (!_osResetSystem && patchContext.GetSdkVersion() >= 0x2004F50)
        {
            _osResetSystem = patchContext.FindPattern32(sOSResetSystemPatternSdk2, sizeof(sOSResetSystemPatternSdk2));
        }
        if (!_osResetSystem && patchContext.GetSdkVersion() >= 0x2004EE9)
        {
            _osResetSystem = patchContext.FindPattern32(sOSResetSystemPatternSdk2Old, sizeof(sOSResetSystemPatternSdk2Old));
        }
        if (!_osResetSystem)
        {
            _osResetSystem = patchContext.FindPattern32(sOSResetSystemPatternPokemonDownloader, sizeof(sOSResetSystemPatternPokemonDownloader));
        }
    }

    if (_osResetSystem)
    {
        LOG_DEBUG("Found end of OS_ResetSystem at %p\n", _osResetSystem);
    }
    else
    {
        LOG_DEBUG("OS_ResetSystem not found\n");
    }

    return true;
}

void OSResetSystemPatch::ApplyPatch(PatchContext& patchContext)
{
    if (!_osResetSystem)
    {
        return;
    }

    // Distance from the matched pattern to the call that sends the reset command to the arm7.
    // That call is replaced, so the game's own reset stops there and the reboot takes over.
    u32 offset;
    if (patchContext.GetSdkVersion().IsTwlSdk())
    {
        // Hybrid roms first check whether they run on a DSi, which puts the call further in.
        offset = _hybrid ? 0x80 : 0x44;
    }
    else
    {
        offset = 0xC;
    }

    // Only a hybrid rom running in DSi mode needs the extra arm7 sync of the reboot.
    auto patchCode = GetOrAddRebootPatchCode(patchContext, _loaderInfo, _hybrid && _runInDSiMode);

    // ldr pc, [pc, #-4]: jumps to the address stored in the word that follows.
    *(u32*)((u8*)_osResetSystem + offset) = 0xE51FF004;
    *(u32*)((u8*)_osResetSystem + offset + 4) = (u32)patchCode->GetOSResetSystemFunction();

    // Pico Loader writes the location of the cheats here, so they stay active after the reset.
    _cheatsPointer = patchCode->GetPart2PatchCode()->GetCheatsPointerAtTarget();
}

const OSResetSystemPatchCode* OSResetSystemPatch::GetOrAddRebootPatchCode(
    PatchContext& patchContext, const loader_info_t* loaderInfo, bool twlArm7Sync)
{
    // Shared, so a second caller gets the same copy instead of placing another one.
    return patchContext.GetPatchCodeCollection().GetOrAddSharedPatchCode([&]
    {
        // Arm7 entry point field of the rom header copy in shared memory (header + 0x34).
        // SDK 5 games keep that copy at 0x02FFFE00, older games at 0x027FFE00.
        patch_osresetsystem_arm7Entry_address = patchContext.GetSdkVersion().IsTwlSdk() ? 0x02FFFE34 : 0x027FFE34;
        if (!twlArm7Sync)
        {
            // Replace the call to the DSi-only arm7 sync with a no-op.
            patch_osresetsystem_entry_jump_to_twl_arm7_sync = THUMB_NOP;
        }

        // The reboot walks the cluster map in the loader info to find Pico Loader on the sd card.
        auto loaderInfoTarget = (loader_info_t*)patchContext.GetPatchHeap().Alloc(sizeof(loader_info_t));
        memcpy(loaderInfoTarget, loaderInfo, sizeof(loader_info_t));

        // Keep this allocation order in sync with GetRebootPatchCodeAllocationSizes.
        auto sdReadPatchCode = patchContext.GetLoaderPlatform()->CreateSdReadPatchCode(
            patchContext.GetPatchCodeCollection(), patchContext.GetPatchHeap());
        auto patchCodePart2 = patchContext.GetPatchCodeCollection().AddUniquePatchCode<OSResetSystemPart2PatchCode>(
            patchContext.GetPatchHeap());
        return new OSResetSystemPatchCode(patchContext.GetPatchHeap(), loaderInfoTarget, sdReadPatchCode, patchCodePart2);
    });
}

u32 OSResetSystemPatch::GetRebootPatchCodeAllocationSizes(u32* sizes)
{
    // Same order as GetOrAddRebootPatchCode allocates them. The sd read code is not listed
    // because the rom read patches have already created it by then.
    sizes[0] = sizeof(loader_info_t);
    sizes[1] = OSResetSystemPart2PatchCode::GetSize();
    sizes[2] = OSResetSystemPatchCode::GetSize();
    return REBOOT_PATCH_CODE_ALLOCATION_COUNT;
}

