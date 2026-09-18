#pragma once
#include "patches/PatchCode.h"
#include "sections.h"

DEFINE_SECTION_SYMBOLS(patch_ingamereset_keycheck);
DEFINE_SECTION_SYMBOLS(patch_ingamereset_dispatch_sdk);
DEFINE_SECTION_SYMBOLS(patch_ingamereset_dispatch_blx);
DEFINE_SECTION_SYMBOLS(patch_ingamereset_dispatch_nested);
DEFINE_SECTION_SYMBOLS(patch_ingamereset_reset);

extern "C" void patch_ingamereset_keyCheck(void);
extern "C" void patch_ingamereset_sdkDispatch(void);
extern "C" void patch_ingamereset_blxDispatch(void);
extern "C" void patch_ingamereset_nestedDispatch(void);
extern "C" void patch_ingamereset_nestedResume(void);
extern "C" void patch_ingamereset_resetEntry(void);

extern u32 patch_ingamereset_resetAddress;
extern u32 patch_ingamereset_sdkIrqTable;
extern u32 patch_ingamereset_sdkIrqReturn;
extern u32 patch_ingamereset_sdkKeyCheck;
extern u32 patch_ingamereset_blxIrqReturn;
extern u32 patch_ingamereset_blxKeyCheck;
extern u32 patch_ingamereset_nestedIrqTable;
extern u32 patch_ingamereset_nestedContinue;
extern u32 patch_ingamereset_nestedResumeAddress;
extern u32 patch_ingamereset_nestedKeyCheck;
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

/// @brief The part of the in-game reset that counts the vblanks during which the reset keys
///        are held. It is shared by every dispatch part.
class InGameResetKeyCheckPatchCode : public PatchCode
{
public:
    InGameResetKeyCheckPatchCode(PatchHeap& patchHeap, const InGameResetResetPatchCode* resetPatchCode)
        : PatchCode(SECTION_START(patch_ingamereset_keycheck), SECTION_SIZE(patch_ingamereset_keycheck), patchHeap)
    {
        patch_ingamereset_resetAddress = (u32)resetPatchCode->GetResetFunction();
    }

    /// @brief Returns the patch heap space this part needs, so callers can check it fits.
    static u32 GetSize()
    {
        return SECTION_SIZE(patch_ingamereset_keycheck);
    }

    const void* GetKeyCheckFunction() const
    {
        return GetAddressAtTarget((void*)patch_ingamereset_keyCheck);
    }
};

/// @brief Base class for the part of the in-game reset that replaces the dispatch of a game's
///        arm9 interrupt dispatcher. There is one subclass per dispatcher version, each in its
///        own IrqDispatcher header, and only the one matching the game is placed in the patch
///        heap.
class InGameResetDispatchPatchCode : public PatchCode
{
public:
    InGameResetDispatchPatchCode(const void* code, u32 size, PatchHeap& patchHeap, const void* entry)
        : PatchCode(code, size, patchHeap), _entry(entry) { }

    /// @brief Returns the address the game's dispatcher must jump to, with the thumb bit set.
    const void* GetDispatchFunction() const
    {
        return GetAddressAtTarget(_entry);
    }

private:
    const void* const _entry;
};
