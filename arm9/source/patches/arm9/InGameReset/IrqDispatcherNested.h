#pragma once
#include "IrqDispatcher.h"
#include "InGameResetPatchCode.h"

/// @brief Dispatch part for the stock dispatcher wrapped so that interrupts can nest. It hooks
///        before the game switches to system mode with irqs enabled, so normal dispatch carries
///        on in the game's own code rather than at the handler.
class InGameResetNestedDispatchPatchCode : public InGameResetDispatchPatchCode
{
public:
    InGameResetNestedDispatchPatchCode(PatchHeap& patchHeap, const IrqDispatcherMatch& match,
        const InGameResetKeyCheckPatchCode* keyCheckPatchCode)
        : InGameResetDispatchPatchCode(SECTION_START(patch_ingamereset_dispatch_nested),
            SECTION_SIZE(patch_ingamereset_dispatch_nested), patchHeap,
            (void*)patch_ingamereset_nestedDispatch)
    {
        patch_ingamereset_nestedIrqTable = match.irqTable;
        patch_ingamereset_nestedContinue = match.continueAddress;
        patch_ingamereset_nestedResumeAddress = (u32)GetAddressAtTarget((void*)patch_ingamereset_nestedResume);
        patch_ingamereset_nestedKeyCheck = (u32)keyCheckPatchCode->GetKeyCheckFunction();
    }

    /// @brief Returns the patch heap space this part needs, so callers can check it fits.
    static u32 GetSize()
    {
        return SECTION_SIZE(patch_ingamereset_dispatch_nested);
    }
};

/// @brief Searches for the stock dispatcher wrapped so that interrupts can nest.
/// @param patchContext The patch context to use.
/// @param result Receives the hook location and addresses when it is found.
/// @return True when it was found, or false otherwise.
bool FindNestedIrqDispatcher(PatchContext& patchContext, IrqDispatcherMatch& result);
