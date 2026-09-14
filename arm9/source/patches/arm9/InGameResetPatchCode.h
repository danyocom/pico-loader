#pragma once
#include "../PatchCode.h"
#include "sections.h"

DEFINE_SECTION_SYMBOLS(patch_ingamereset);
DEFINE_SECTION_SYMBOLS(patch_ingamereset_reset);

extern "C" void patch_ingamereset_irqDispatch(void);
extern "C" void patch_ingamereset_resetEntry(void);

extern u32 patch_ingamereset_irqTable;
extern u32 patch_ingamereset_irqReturn;
extern u32 patch_ingamereset_resetAddress;
extern u32 patch_ingamereset_slot1LockAddress;
extern u32 patch_ingamereset_resetParamAddress;
extern u32 patch_ingamereset_resetParam;
extern u32 patch_ingamereset_resetSystemEntry;

/// @brief The part of the in-game reset that runs once the reset keys have been held long enough.
class InGameResetResetPatchCode : public PatchCode
{
public:
    InGameResetResetPatchCode(PatchHeap& patchHeap, u32 slot1LockAddress,
        u32 resetParamAddress, u32 resetParam, const void* resetSystemEntry)
        : PatchCode(SECTION_START(patch_ingamereset_reset), SECTION_SIZE(patch_ingamereset_reset), patchHeap)
    {
        patch_ingamereset_slot1LockAddress = slot1LockAddress;
        patch_ingamereset_resetParamAddress = resetParamAddress;
        patch_ingamereset_resetParam = resetParam;
        patch_ingamereset_resetSystemEntry = (u32)resetSystemEntry;
    }

    /// @brief Returns the patch heap space this part needs, so callers can check it fits.
    static u32 GetSize()
    {
        return SECTION_SIZE(patch_ingamereset_reset);
    }

    const void* GetResetFunction() const
    {
        return GetAddressAtTarget((void*)patch_ingamereset_resetEntry);
    }
};

/// @brief The part of the in-game reset that replaces the dispatch of the SDK interrupt dispatcher.
class InGameResetPatchCode : public PatchCode
{
public:
    InGameResetPatchCode(PatchHeap& patchHeap, u32 irqTable, u32 irqReturn,
        const InGameResetResetPatchCode* resetPatchCode)
        : PatchCode(SECTION_START(patch_ingamereset), SECTION_SIZE(patch_ingamereset), patchHeap)
    {
        patch_ingamereset_irqTable = irqTable;
        patch_ingamereset_irqReturn = irqReturn;
        patch_ingamereset_resetAddress = (u32)resetPatchCode->GetResetFunction();
    }

    /// @brief Returns the patch heap space this part needs, so callers can check it fits.
    static u32 GetSize()
    {
        return SECTION_SIZE(patch_ingamereset);
    }

    const void* GetIrqDispatchFunction() const
    {
        return GetAddressAtTarget((void*)patch_ingamereset_irqDispatch);
    }
};
