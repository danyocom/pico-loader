#pragma once
#include "../PatchCode.h"
#include "sections.h"
#include "LoaderInfo.h"
#include "patches/platform/IReadSectorsPatchCode.h"

DEFINE_SECTION_SYMBOLS(patch_osresetsystem);
DEFINE_SECTION_SYMBOLS(patch_osresetsystem_boot);

extern "C" void patch_osresetsystem_entry(void);
extern "C" void patch_osresetsystem_bootPicoLoader(void);

extern const loader_info_t* patch_osresetsystem_loader_info_address;
extern u32 patch_osresetsystem_readSdSectors_address;
extern u32 patch_osresetsystem_bootPicoLoader_address;
extern u16 patch_osresetsystem_entry_jump_to_twl_arm7_sync;
extern u32 patch_osresetsystem_arm7Entry_address;
extern u32 patch_osresetsystem_cheats_address;

class OSResetSystemPart2PatchCode : public PatchCode
{
public:
    explicit OSResetSystemPart2PatchCode(PatchHeap& patchHeap)
        : PatchCode(SECTION_START(patch_osresetsystem_boot), SECTION_SIZE(patch_osresetsystem_boot), patchHeap)
    {
    }

    const void* GetOSResetSystemPart2Function() const
    {
        return GetAddressAtTarget((void*)patch_osresetsystem_bootPicoLoader);
    }

    void** GetCheatsPointerAtTarget() const
    {
        return (void**)GetAddressAtTarget(&patch_osresetsystem_cheats_address);
    }

    /// @brief Returns the patch heap space this part needs, so callers can check it fits.
    static u32 GetSize()
    {
        return SECTION_SIZE(patch_osresetsystem_boot);
    }
};

class OSResetSystemPatchCode : public PatchCode
{
public:
    OSResetSystemPatchCode(PatchHeap& patchHeap, const loader_info_t* loaderInfo,
        const IReadSectorsPatchCode* readSectorsPatchCode, const OSResetSystemPart2PatchCode* part2PatchCode)
        : PatchCode(SECTION_START(patch_osresetsystem), SECTION_SIZE(patch_osresetsystem), patchHeap)
        , _part2PatchCode(part2PatchCode)
    {
        patch_osresetsystem_loader_info_address = loaderInfo;
        patch_osresetsystem_readSdSectors_address = (u32)readSectorsPatchCode->GetReadSectorsFunction();
        patch_osresetsystem_bootPicoLoader_address = (u32)part2PatchCode->GetOSResetSystemPart2Function();
    }

    const void* GetOSResetSystemFunction() const
    {
        return GetAddressAtTarget((void*)patch_osresetsystem_entry);
    }

    /// @brief Returns the second part of the reboot, which holds the cheats pointer.
    const OSResetSystemPart2PatchCode* GetPart2PatchCode() const
    {
        return _part2PatchCode;
    }

    /// @brief Returns the patch heap space this part needs, so callers can check it fits.
    static u32 GetSize()
    {
        return SECTION_SIZE(patch_osresetsystem);
    }

private:
    // Kept so a caller that did not create the reboot can still reach its second part.
    const OSResetSystemPart2PatchCode* _part2PatchCode;
};
