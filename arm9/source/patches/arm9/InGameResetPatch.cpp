#include "common.h"
#include <array>
#include "patches/PatchContext.h"
#include "sharedMemory.h"
#include "inGameReset.h"
#include "OSResetSystemPatch.h"
#include "OSResetSystemPatchCode.h"
#include "InGameResetPatchCode.h"
#include "InGameResetPatch.h"

// Last four instructions of the SDK interrupt dispatcher (OS_IrqHandler), followed in
// its literal pool by the address of the interrupt table and the address the handler
// returns to. The same code is used from SDK 2 through SDK 5, on both cpus.
static const u32 sIrqDispatchPattern[] =
{
    0xE59F1008u, // ldr r1, =irqTable
    0xE7910100u, // ldr r0, [r1, r0, lsl #2]
    0xE59FE004u, // ldr lr, =irqReturn
    0xE12FFF10u  // bx r0
};

// Start of the same dispatcher: fetching REG_IE, which is not something unrelated code
// ending in the same four instructions would do.
static const u32 sIrqHandlerStart[] =
{
    0xE92D4000u, // push {lr}
    0xE3A0C301u, // mov r12, #0x04000000
    0xE28CCE21u  // add r12, r12, #0x210
};

// How many words before the dispatch to look for the start of the same dispatcher. This
// covers the dispatcher's own length with slack, without reaching far into other code.
#define IRQ_HANDLER_MAX_LENGTH_WORDS    32

// Word positions after the four matched instructions: word 4 is the irqTable literal and
// word 5 the irqReturn literal. irqReturn is the instruction right after those two literals,
// so 4 instructions + 2 literals = 6 words = 0x18 bytes past the start of the match.
#define IRQ_DISPATCH_IRQ_TABLE_WORD     4
#define IRQ_DISPATCH_IRQ_RETURN_WORD    5
#define IRQ_DISPATCH_IRQ_RETURN_OFFSET  0x18

// Looks backwards from the dispatch for the dispatcher's first three instructions.
static bool hasIrqHandlerStart(const u32* irqDispatch)
{
    for (u32 i = 1; i <= IRQ_HANDLER_MAX_LENGTH_WORDS; i++)
    {
        const u32* candidate = irqDispatch - i;
        if (candidate[0] == sIrqHandlerStart[0] &&
            candidate[1] == sIrqHandlerStart[1] &&
            candidate[2] == sIrqHandlerStart[2])
        {
            return true;
        }
    }
    return false;
}

bool InGameResetPatch::FindPatchTarget(PatchContext& patchContext)
{
    _irqDispatch = nullptr;

    u32* irqDispatch = patchContext.FindPattern32(sIrqDispatchPattern, sizeof(sIrqDispatchPattern));
    if (!irqDispatch)
    {
        LOG_DEBUG("In-game reset: SDK irq dispatcher not found\n");
        return true;
    }

    // A second match means it is unclear which one is the real dispatcher, so patch neither.
    if (patchContext.FindPattern32(sIrqDispatchPattern, sizeof(sIrqDispatchPattern), irqDispatch + 1))
    {
        LOG_WARNING("In-game reset: SDK irq dispatcher is ambiguous\n");
        return true;
    }

    if (!hasIrqHandlerStart(irqDispatch))
    {
        LOG_WARNING("In-game reset: irq dispatcher start not found\n");
        return true;
    }

    // The dispatcher normally lives in ITCM and is copied there by autoload when the game
    // starts. Its literal return address is where the instruction after the dispatch ends
    // up, which ties the match to the real dispatcher at its final location.
    auto autoloadAdjuster = patchContext.GetAutoloadAdjuster();
    if (!autoloadAdjuster)
    {
        LOG_WARNING("In-game reset: no autoload information\n");
        return true;
    }

    u32 finalAddress = autoloadAdjuster->AdjustInitialToFinal((u32)irqDispatch);
    if (irqDispatch[IRQ_DISPATCH_IRQ_RETURN_WORD] != finalAddress + IRQ_DISPATCH_IRQ_RETURN_OFFSET)
    {
        LOG_WARNING("In-game reset: irq dispatcher return address mismatch\n");
        return true;
    }

    // The table is an array of 32-bit handler addresses, so its address must be word aligned.
    u32 irqTable = irqDispatch[IRQ_DISPATCH_IRQ_TABLE_WORD];
    if (irqTable == 0 || (irqTable & 3) != 0)
    {
        LOG_WARNING("In-game reset: invalid irq table address 0x%X\n", irqTable);
        return true;
    }

    LOG_DEBUG("In-game reset: irq dispatch at 0x%p (final 0x%X), table 0x%X\n",
        irqDispatch, finalAddress, irqTable);
    _irqDispatch = irqDispatch;
    return true;
}

void InGameResetPatch::ApplyPatch(PatchContext& patchContext)
{
    if (!_irqDispatch)
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
    // Room for the reboot's allocations plus the two in-game reset parts.
    std::array<u32, OSResetSystemPatch::REBOOT_PATCH_CODE_ALLOCATION_COUNT + 2> sizes;
    u32 count = 0;
    if (createsReboot)
    {
        count = OSResetSystemPatch::GetRebootPatchCodeAllocationSizes(sizes.data());
    }
    sizes[count++] = InGameResetResetPatchCode::GetSize();
    sizes[count++] = InGameResetPatchCode::GetSize();
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
    auto patchCode = patchContext.GetPatchCodeCollection().AddUniquePatchCode<InGameResetPatchCode>
    (
        patchContext.GetPatchHeap(),
        _irqDispatch[IRQ_DISPATCH_IRQ_TABLE_WORD],
        _irqDispatch[IRQ_DISPATCH_IRQ_RETURN_WORD],
        resetPatchCode
    );

    // Overwrite the first two of the four dispatch instructions with a jump. r0 still holds
    // the irq index at that point, and the remaining two instructions are never reached.
    // 0xE51FF004 is ldr pc, [pc, #-4]: load pc from the word that follows it.
    _irqDispatch[0] = 0xE51FF004;
    _irqDispatch[1] = (u32)patchCode->GetIrqDispatchFunction();
    LOG_DEBUG("In-game reset enabled\n");
}

