.cpu arm946e-s
.syntax unified

// L + R + Down + B, as the bits that read 0 in REG_KEYINPUT while held.
// Bit 9 = L, bit 8 = R, bit 7 = Down, bit 1 = B, giving 0x382.
.equ IN_GAME_RESET_KEY_MASK, (1 << 9) | (1 << 8) | (1 << 7) | (1 << 1)

// Number of consecutive vblanks the keys must be held before the reset is performed.
// The DS draws 60 frames per second, so 120 is two seconds.
.equ IN_GAME_RESET_HOLD_FRAMES, 120

// Lock id used while holding the slot 1 lock. The SDK hands out ids starting at 0x40 and
// the game never runs again once the lock is taken, so any nonzero id is sufficient.
.equ IN_GAME_RESET_LOCK_ID, 0x7D

// ARM9 patch space is usually made of the gaps between the secure area syscall thunks,
// most of which are around 0x70 bytes. The code is therefore split in two small parts that
// are placed independently: the dispatch that runs on every interrupt, and the reset.

.section "patch_ingamereset", "ax"

// Replaces the last four instructions of the SDK interrupt dispatcher (OS_IrqHandler):
//
//     ldr r1, =irqTable
//     ldr r0, [r1, r0, lsl #2]
//     ldr lr, =irqReturn
//     bx  r0
//
// It is entered with r0 holding the index of the interrupt being serviced and performs
// the same dispatch, counting on the way the vblanks during which the reset keys are held.
// r0-r3 and r12 are free to use, as they are for the handler being dispatched to.

.thumb
.global patch_ingamereset_irqDispatch
.type patch_ingamereset_irqDispatch, %function
patch_ingamereset_irqDispatch:
    // Same as the replaced code: look up the handler for irq r0 and set its return address.
    ldr r1, patch_ingamereset_irqTable
    lsls r2, r0, #2 // table entries are 4 bytes each
    ldr r2, [r1, r2] // r2 = handler
    ldr r1, patch_ingamereset_irqReturn
    mov lr, r1
    cmp r0, #0 // irq 0 is vblank, and only vblank advances the count
    bne callHandler

    ldr r0, regKeyInput
    ldrh r0, [r0]
    // Thumb can only load an 8-bit constant, so the 10-bit key mask is built in two steps:
    // 0x38 << 4 = 0x380, then + 0x2 = 0x382.
    movs r1, #(IN_GAME_RESET_KEY_MASK >> 4)
    lsls r1, r1, #4
    adds r1, #(IN_GAME_RESET_KEY_MASK & 0xF)
    adr r3, heldFrames
    tst r0, r1 // keys read 0 while pressed, so any 1 bit means a key is released
    bne keysReleased

    // All keys held: count this frame, or reset once the limit is reached.
    ldrh r0, [r3]
    cmp r0, #IN_GAME_RESET_HOLD_FRAMES
    bhs tryReset
    adds r0, #1
    b storeHeldFrames

keysReleased:
    movs r0, #0
storeHeldFrames:
    strh r0, [r3]

callHandler:
    // The handler is entered with its address in r0, as the replaced bx r0 did.
    movs r0, r2
    bx r0

tryReset:
    // r2 and lr are still set up for dispatching, in case the reset has to wait
    ldr r3, patch_ingamereset_resetAddress
    bx r3

.balign 4

// Number of vblanks the keys have been held so far. Written 16 bits wide.
heldFrames:
    .word 0

// REG_KEYINPUT: one bit per button, 0 while pressed.
regKeyInput:
    .word 0x04000130

.global patch_ingamereset_irqTable
patch_ingamereset_irqTable:
    .word 0

.global patch_ingamereset_irqReturn
patch_ingamereset_irqReturn:
    .word 0

.global patch_ingamereset_resetAddress
patch_ingamereset_resetAddress:
    .word 0

.pool

.section "patch_ingamereset_reset", "ax"

// Entered from the dispatch with r2 = the vblank handler and lr = the dispatcher's return
// address. Returns into the handler when the reset cannot be performed yet.
//
// The cpu is in irq mode with interrupts disabled, and nothing from here on returns into
// the game or enables interrupts before Pico Loader has started.

.thumb
.global patch_ingamereset_resetEntry
.type patch_ingamereset_resetEntry, %function
patch_ingamereset_resetEntry:
    // Card access only ever happens with the slot 1 lock held, by either cpu. Taking the
    // lock guarantees no rom read or save operation is left half done on the cartridge
    // bus. This mirrors the SDK's own try-lock: swap the lock id into the lock flag, and
    // the lock is ours only if the flag was clear. A holder that is overwritten this way
    // clears the flag when it unlocks, exactly as it would after a failed SDK attempt.
    // The SDK's OS_ResetSystem likewise holds this lock while resetting the arm7.
    // The lock is 8 bytes: a 32-bit lock flag at +0, then the 16-bit owner id at +4.
    ldr r1, patch_ingamereset_slot1LockAddress
    ldrh r0, [r1, #4] // owner id, 0 when nobody holds the lock
    cmp r0, #0
    bne waitForLock
    movs r0, #IN_GAME_RESET_LOCK_ID
    // swp only exists in ARM code. bx pc from a word-aligned address jumps 4 bytes ahead,
    // past the nop, and switches to ARM because bit 0 of the target is clear.
    .balign 4
    bx pc
    nop

.arm
    swp r0, r0, [r1] // in one step: write our id to the lock flag, r0 = previous flag
    // pc reads 8 bytes ahead in ARM code, which is the instruction after bx r3. Adding 1
    // sets bit 0, so bx switches back to Thumb.
    add r3, pc, #1
    bx r3

.thumb
    cmp r0, #0 // a nonzero previous flag means someone else took the lock first
    bne waitForLock
    movs r0, #IN_GAME_RESET_LOCK_ID
    strh r0, [r1, #4]

    // The SDK gives both slots to the arm7 whenever they are unlocked, while the reboot
    // reads Pico Loader from the sd card through the flashcard's slot on the arm9.
    // In REG_EXMEMCNT, bit 11 selects the slot 1 owner and bit 7 the slot 2 owner, where 0
    // is the arm9. The mask 0x88 << 4 = 0x880 clears both.
    ldr r1, regExMemCnt
    ldrh r0, [r1]
    movs r3, #0x88
    lsls r3, r3, #4
    bics r0, r3
    strh r0, [r1]

    // A dma transfer left running could write over the memory the reboot uses.
    // Clearing the low 12 bits of 0x04000204 gives 0x04000000, the start of the io
    // registers, and the dma registers start at 0x040000B0.
    lsrs r1, r1, #12
    lsls r1, r1, #12
    adds r1, #0xB0
    movs r0, #0 // writing 0 to a DMAxCNT stops that transfer
    str r0, [r1, #0x08] // REG_DMA0CNT
    str r0, [r1, #0x14] // REG_DMA1CNT
    str r0, [r1, #0x20] // REG_DMA2CNT
    str r0, [r1, #0x2C] // REG_DMA3CNT

    // Leave a marker for Pico Loader's arm7, telling it to load the launcher instead of
    // restarting the game.
    ldr r1, patch_ingamereset_resetParamAddress
    ldr r0, patch_ingamereset_resetParam
    str r0, [r1]

    // The dispatcher's stack remains valid for the reboot.
    ldr r0, patch_ingamereset_resetSystemEntry
    bx r0

waitForLock:
    // Try again on the next vblank; the hold count stays at its limit.
    movs r0, r2
    bx r0

.balign 4

// REG_EXMEMCNT: selects which cpu owns each cartridge slot.
regExMemCnt:
    .word 0x04000204

.global patch_ingamereset_slot1LockAddress
patch_ingamereset_slot1LockAddress:
    .word 0

.global patch_ingamereset_resetParamAddress
patch_ingamereset_resetParamAddress:
    .word 0

.global patch_ingamereset_resetParam
patch_ingamereset_resetParam:
    .word 0

.global patch_ingamereset_resetSystemEntry
patch_ingamereset_resetSystemEntry:
    .word 0

.pool

.end
