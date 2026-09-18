#pragma once
#include "IrqDispatcher.h"
#include "InGameResetPatchCode.h"

/// @brief Dispatch part for the stock SDK interrupt dispatcher.
class InGameResetSdkDispatchPatchCode : public InGameResetDispatchPatchCode
{
public:
    InGameResetSdkDispatchPatchCode(PatchHeap& patchHeap, const IrqDispatcherMatch& match,
        const InGameResetKeyCheckPatchCode* keyCheckPatchCode)
        : InGameResetDispatchPatchCode(SECTION_START(patch_ingamereset_dispatch_sdk),
            SECTION_SIZE(patch_ingamereset_dispatch_sdk), patchHeap,
            (void*)patch_ingamereset_sdkDispatch)
    {
        patch_ingamereset_sdkIrqTable = match.irqTable;
        patch_ingamereset_sdkIrqReturn = match.continueAddress;
        patch_ingamereset_sdkKeyCheck = (u32)keyCheckPatchCode->GetKeyCheckFunction();
    }

    /// @brief Returns the patch heap space this part needs, so callers can check it fits.
    static u32 GetSize()
    {
        return SECTION_SIZE(patch_ingamereset_dispatch_sdk);
    }
};

/// @brief Searches for the stock SDK interrupt dispatcher.
/// @param patchContext The patch context to use.
/// @param result Receives the hook location and addresses when it is found.
/// @return True when it was found, or false otherwise.
bool FindSdkIrqDispatcher(PatchContext& patchContext, IrqDispatcherMatch& result);
