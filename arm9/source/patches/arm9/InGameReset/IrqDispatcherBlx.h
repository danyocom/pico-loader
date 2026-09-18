#pragma once
#include "IrqDispatcher.h"
#include "InGameResetPatchCode.h"

/// @brief Dispatch part for a dispatcher that picks the interrupt with a single clz and calls
///        the handler with blx. It needs no interrupt table address, because the dispatcher
///        already holds one in a register at the point where this takes over.
class InGameResetBlxDispatchPatchCode : public InGameResetDispatchPatchCode
{
public:
    InGameResetBlxDispatchPatchCode(PatchHeap& patchHeap, const IrqDispatcherMatch& match,
        const InGameResetKeyCheckPatchCode* keyCheckPatchCode)
        : InGameResetDispatchPatchCode(SECTION_START(patch_ingamereset_dispatch_blx),
            SECTION_SIZE(patch_ingamereset_dispatch_blx), patchHeap,
            (void*)patch_ingamereset_blxDispatch)
    {
        patch_ingamereset_blxIrqReturn = match.continueAddress;
        patch_ingamereset_blxKeyCheck = (u32)keyCheckPatchCode->GetKeyCheckFunction();
    }

    /// @brief Returns the patch heap space this part needs, so callers can check it fits.
    static u32 GetSize()
    {
        return SECTION_SIZE(patch_ingamereset_dispatch_blx);
    }
};

/// @brief Searches for a dispatcher that picks the interrupt with a single clz and calls the
///        handler with blx.
/// @param patchContext The patch context to use.
/// @param result Receives the hook location and addresses when it is found.
/// @return True when it was found, or false otherwise.
bool FindBlxIrqDispatcher(PatchContext& patchContext, IrqDispatcherMatch& result);
