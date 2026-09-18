#pragma once
#include "../PatchCode.h"
#include "sections.h"

DEFINE_SECTION_SYMBOLS(patch_ingamereset_keycheck);
DEFINE_SECTION_SYMBOLS(patch_ingamereset_dispatch_sdk);
DEFINE_SECTION_SYMBOLS(patch_ingamereset_dispatch_blx);
DEFINE_SECTION_SYMBOLS(patch_ingamereset_reset);

extern "C" void patch_ingamereset_keyCheck(void);
extern "C" void patch_ingamereset_sdkDispatch(void);
extern "C" void patch_ingamereset_blxDispatch(void);
extern "C" void patch_ingamereset_resetEntry(void);

extern u32 patch_ingamereset_resetAddress;
extern u32 patch_ingamereset_sdkIrqTable;
extern u32 patch_ingamereset_sdkIrqReturn;
extern u32 patch_ingamereset_sdkKeyCheck;
extern u32 patch_ingamereset_blxIrqReturn;
extern u32 patch_ingamereset_blxKeyCheck;
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
///        arm9 interrupt dispatcher. There is one of these per dispatcher version, and only
///        the one matching the game is placed in the patch heap.
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

/// @brief Dispatch part for the stock SDK interrupt dispatcher.
class InGameResetSdkDispatchPatchCode : public InGameResetDispatchPatchCode
{
public:
    InGameResetSdkDispatchPatchCode(PatchHeap& patchHeap, u32 irqTable, u32 irqReturn,
        const InGameResetKeyCheckPatchCode* keyCheckPatchCode)
        : InGameResetDispatchPatchCode(SECTION_START(patch_ingamereset_dispatch_sdk),
            SECTION_SIZE(patch_ingamereset_dispatch_sdk), patchHeap,
            (void*)patch_ingamereset_sdkDispatch)
    {
        patch_ingamereset_sdkIrqTable = irqTable;
        patch_ingamereset_sdkIrqReturn = irqReturn;
        patch_ingamereset_sdkKeyCheck = (u32)keyCheckPatchCode->GetKeyCheckFunction();
    }

    /// @brief Returns the patch heap space this part needs, so callers can check it fits.
    static u32 GetSize()
    {
        return SECTION_SIZE(patch_ingamereset_dispatch_sdk);
    }
};

/// @brief Dispatch part for a dispatcher that picks the interrupt with a single clz and calls
///        the handler with blx. It needs no interrupt table address, because the dispatcher
///        already holds one in a register at the point where this takes over.
class InGameResetBlxDispatchPatchCode : public InGameResetDispatchPatchCode
{
public:
    InGameResetBlxDispatchPatchCode(PatchHeap& patchHeap, u32 irqReturn,
        const InGameResetKeyCheckPatchCode* keyCheckPatchCode)
        : InGameResetDispatchPatchCode(SECTION_START(patch_ingamereset_dispatch_blx),
            SECTION_SIZE(patch_ingamereset_dispatch_blx), patchHeap,
            (void*)patch_ingamereset_blxDispatch)
    {
        patch_ingamereset_blxIrqReturn = irqReturn;
        patch_ingamereset_blxKeyCheck = (u32)keyCheckPatchCode->GetKeyCheckFunction();
    }

    /// @brief Returns the patch heap space this part needs, so callers can check it fits.
    static u32 GetSize()
    {
        return SECTION_SIZE(patch_ingamereset_dispatch_blx);
    }
};
