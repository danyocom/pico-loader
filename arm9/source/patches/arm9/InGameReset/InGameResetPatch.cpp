#include "common.h"
#include <array>
#include "patches/PatchContext.h"
#include "sharedMemory.h"
#include "inGameReset.h"
#include "../OSResetSystemPatch.h"
#include "../OSResetSystemPatchCode.h"
#include "InGameResetPatchCode.h"
#include "IrqDispatcherSdk.h"
#include "IrqDispatcherBlx.h"
#include "IrqDispatcherNested.h"
#include "InGameResetPatch.h"

bool InGameResetPatch::FindPatchTarget(PatchContext& patchContext)
{
    // Tried in order, stopping at the first that matches. The stock dispatcher is first
    // because nearly every game has it.
    if (FindSdkIrqDispatcher(patchContext, _irqDispatcherMatch))
    {
        _irqDispatcherVariant = IrqDispatcherVariant::Sdk;
    }
    else if (FindBlxIrqDispatcher(patchContext, _irqDispatcherMatch))
    {
        _irqDispatcherVariant = IrqDispatcherVariant::Blx;
    }
    else if (FindNestedIrqDispatcher(patchContext, _irqDispatcherMatch))
    {
        _irqDispatcherVariant = IrqDispatcherVariant::Nested;
    }
    else
    {
        LOG_DEBUG("In-game reset: no known irq dispatcher found\n");
    }
    return true;
}

u32 InGameResetPatch::GetDispatchPatchCodeSize() const
{
    switch (_irqDispatcherVariant)
    {
        case IrqDispatcherVariant::Sdk:    return InGameResetSdkDispatchPatchCode::GetSize();
        case IrqDispatcherVariant::Blx:    return InGameResetBlxDispatchPatchCode::GetSize();
        case IrqDispatcherVariant::Nested: return InGameResetNestedDispatchPatchCode::GetSize();
        default:                           return 0;
    }
}

const InGameResetDispatchPatchCode* InGameResetPatch::CreateDispatchPatchCode(
    PatchContext& patchContext, const InGameResetKeyCheckPatchCode* keyCheckPatchCode) const
{
    auto& patchCodeCollection = patchContext.GetPatchCodeCollection();
    auto& patchHeap = patchContext.GetPatchHeap();
    switch (_irqDispatcherVariant)
    {
        case IrqDispatcherVariant::Sdk:
            return patchCodeCollection.AddUniquePatchCode<InGameResetSdkDispatchPatchCode>(
                patchHeap, _irqDispatcherMatch, keyCheckPatchCode);
        case IrqDispatcherVariant::Blx:
            return patchCodeCollection.AddUniquePatchCode<InGameResetBlxDispatchPatchCode>(
                patchHeap, _irqDispatcherMatch, keyCheckPatchCode);
        case IrqDispatcherVariant::Nested:
            return patchCodeCollection.AddUniquePatchCode<InGameResetNestedDispatchPatchCode>(
                patchHeap, _irqDispatcherMatch, keyCheckPatchCode);
        default:
            return nullptr;
    }
}

void InGameResetPatch::ApplyPatch(PatchContext& patchContext)
{
    if (_irqDispatcherVariant == IrqDispatcherVariant::None)
    {
        return;
    }

    // When the game's own OS_ResetSystem was patched the reboot already exists and is reused.
    bool createsReboot = !_osResetSystemPatch->IsApplied();
    if (createsReboot && !_hasSdReadPatchCode)
    {
        // The size of the platform sd read code is not known up front, so its allocation
        // cannot be checked below.
        LOG_WARNING("In-game reset: no sd read code to reuse\n");
        return;
    }

    // Running out of patch heap is not recoverable, and a game must never fail to boot
    // because this optional patch could not be placed. The check replays every allocation
    // made below, in order.
    // Room for the reboot's allocations plus the three in-game reset parts.
    std::array<u32, OSResetSystemPatch::REBOOT_PATCH_CODE_ALLOCATION_COUNT + 3> sizes;
    u32 count = 0;
    if (createsReboot)
    {
        count = OSResetSystemPatch::GetRebootPatchCodeAllocationSizes(sizes.data());
    }
    sizes[count++] = InGameResetResetPatchCode::GetSize();
    sizes[count++] = InGameResetKeyCheckPatchCode::GetSize();
    sizes[count++] = GetDispatchPatchCodeSize();
    if (!patchContext.GetPatchHeap().CanAllocAll(sizes.data(), count))
    {
        LOG_WARNING("In-game reset: not enough patch space\n");
        return;
    }

    auto rebootPatchCode = OSResetSystemPatch::GetOrAddRebootPatchCode(patchContext, _loaderInfo, _twlArm7Sync);

    // Shared memory holds the slot 1 lock and the reset parameter. SDK 5 games keep it at
    // 0x02FFF800, older games at 0x027FF800.
    auto sharedMemory = patchContext.GetSdkVersion().IsTwlSdk() ? NTR_SHARED_MEMORY_SDK5 : NTR_SHARED_MEMORY;
    auto resetPatchCode = patchContext.GetPatchCodeCollection().AddUniquePatchCode<InGameResetResetPatchCode>
    (
        patchContext.GetPatchHeap(),
        (u32)&sharedMemory->slot1Lock,
        (u32)&sharedMemory->resetParam,
        (u32)IN_GAME_RESET_PARAM_RETURN_TO_LAUNCHER,
        rebootPatchCode->GetOSResetSystemFunction()
    );
    auto keyCheckPatchCode = patchContext.GetPatchCodeCollection().AddUniquePatchCode<InGameResetKeyCheckPatchCode>
    (
        patchContext.GetPatchHeap(),
        resetPatchCode
    );
    auto dispatchPatchCode = CreateDispatchPatchCode(patchContext, keyCheckPatchCode);

    // Overwrite two of the dispatcher's instructions with a jump to the dispatch part.
    // 0xE51FF004 is ldr pc, [pc, #-4]: load pc from the word that follows it.
    _irqDispatcherMatch.hook[0] = 0xE51FF004;
    _irqDispatcherMatch.hook[1] = (u32)dispatchPatchCode->GetDispatchFunction();
    LOG_DEBUG("In-game reset enabled\n");
}
