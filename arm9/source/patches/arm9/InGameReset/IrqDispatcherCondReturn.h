#pragma once
#include "IrqDispatcher.h"
#include "InGameResetPatchCode.h"

/// @brief Dispatch part for a dispatcher that sets the handler's return address only for some
///        interrupts. It needs no interrupt table address, because the dispatcher has already
///        looked the handler up by the point where this takes over.
class InGameResetCondReturnDispatchPatchCode : public InGameResetDispatchPatchCode
{
public:
    InGameResetCondReturnDispatchPatchCode(PatchHeap& patchHeap, const IrqDispatcherMatch& match,
        const InGameResetKeyCheckPatchCode* keyCheckPatchCode)
        : InGameResetDispatchPatchCode(SECTION_START(patch_ingamereset_dispatch_condreturn),
            SECTION_SIZE(patch_ingamereset_dispatch_condreturn), patchHeap,
            (void*)patch_ingamereset_condReturnDispatch)
    {
        patch_ingamereset_condReturnIrqReturn = match.continueAddress;
        patch_ingamereset_condReturnResumeAddress =
            (u32)GetAddressAtTarget((void*)patch_ingamereset_condReturnResume);
        patch_ingamereset_condReturnKeyCheck = (u32)keyCheckPatchCode->GetKeyCheckFunction();
    }

    /// @brief Returns the patch heap space this part needs, so callers can check it fits.
    static u32 GetSize()
    {
        return SECTION_SIZE(patch_ingamereset_dispatch_condreturn);
    }
};

/// @brief Searches for a dispatcher that sets the handler's return address only for some
///        interrupts.
/// @param patchContext The patch context to use.
/// @param result Receives the hook location and addresses when it is found.
/// @return True when it was found, or false otherwise.
bool FindCondReturnIrqDispatcher(PatchContext& patchContext, IrqDispatcherMatch& result);
