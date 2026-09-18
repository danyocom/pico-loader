.cpu arm946e-s
.syntax unified

// L + R + Down + B + A, as the bits that read 0 in REG_KEYINPUT while held.
// Bit 9 = L, bit 8 = R, bit 7 = Down, bit 1 = B, bit 0 = A, giving 0x383.
.equ IN_GAME_RESET_KEY_MASK, (1 << 9) | (1 << 8) | (1 << 7) | (1 << 1) | (1 << 0)

// Number of consecutive vblanks the keys must be held before the reset is performed.
// The DS draws 60 frames per second, so 60 is one second.
.equ IN_GAME_RESET_HOLD_FRAMES, 60

// Lock id used while holding the slot 1 lock. The SDK hands out ids starting at 0x40 and
// the game never runs again once the lock is taken, so any nonzero id is sufficient.
.equ IN_GAME_RESET_LOCK_ID, 0x7D

// ARM9 patch space is usually made of the gaps between the secure area syscall thunks,
// most of which are around 0x70 bytes. The code is therefore split into small parts that
// are placed independently.
//
// Games do not all use the same arm9 interrupt dispatcher, so the part that replaces the
// dispatch is chosen per game and only the chosen one is placed. Those dispatch parts
// share the key check and the reset through one convention:
//
//   A dispatch part replaces the handler lookup of a game's interrupt dispatcher. It is
//   entered in thumb state with the cpu in irq mode and irqs masked. It must leave in r2
//   the address to branch to in order to carry on with normal dispatch, along with any
//   other register that continuation needs, and then branch to the key check for a vblank
//   interrupt, or enter the continuation directly for any other interrupt.
//
//   The key check is entered with r2 set that way. It either enters the continuation or
//   enters the reset, which leaves r2 alone so that it can return to the continuation when
//   the reset cannot be performed yet.
//
//   Entering a continuation is always "movs r0, r2; bx r0", so r2 must carry the thumb bit
//   when the continuation is thumb code.
//
// Adding support for another dispatcher therefore means adding a dispatch part and nothing
// else. r0-r3 and r12 are free to use in all of them, as they are for the handler being
// dispatched to.

.section "patch_ingamereset_keycheck", "ax"

// Counts the vblanks during which the reset keys are held, and enters the reset once they
// have been held long enough. Entered from a dispatch part on vblank, with r2 holding the
// address that carries on with normal dispatch.

.thumb
.global patch_ingamereset_keyCheck
.type patch_ingamereset_keyCheck, %function
patch_ingamereset_keyCheck:
    ldr r0, regKeyInput
    ldrh r0, [r0]
    // Thumb can only load an 8-bit constant, so the 10-bit key mask is built in two steps:
    // 0x38 << 4 = 0x380, then + 0x3 = 0x383.
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
    movs r0, r2
    bx r0

tryReset:
    // r2 is left as it is, so that the reset can return to it if it has to wait.
    ldr r3, patch_ingamereset_resetAddress
    bx r3

.balign 4

// Number of vblanks the keys have been held so far. Written 16 bits wide.
heldFrames:
    .word 0

// REG_KEYINPUT: one bit per button, 0 while pressed.
regKeyInput:
    .word 0x04000130

.global patch_ingamereset_resetAddress
patch_ingamereset_resetAddress:
    .word 0

.pool

.section "patch_ingamereset_dispatch_sdk", "ax"

// Dispatch part for the stock SDK interrupt dispatcher (OS_IrqHandler), which is used by
// almost every retail game from SDK 2 through SDK 5. It replaces the last four
// instructions:
//
//     ldr r1, =irqTable
//     ldr r0, [r1, r0, lsl #2]
//     ldr lr, =irqReturn
//     bx  r0
//
// and is entered with r0 holding the index of the interrupt being serviced.

.thumb
.global patch_ingamereset_sdkDispatch
.type patch_ingamereset_sdkDispatch, %function
patch_ingamereset_sdkDispatch:
    // Same as the replaced code: look up the handler for irq r0 and set its return address.
    ldr r1, patch_ingamereset_sdkIrqTable
    lsls r2, r0, #2 // table entries are 4 bytes each
    ldr r2, [r1, r2] // r2 = handler, which is where normal dispatch carries on
    ldr r1, patch_ingamereset_sdkIrqReturn
    mov lr, r1
    cmp r0, #0 // irq 0 is vblank, and only vblank advances the count
    bne sdkContinue
    ldr r3, patch_ingamereset_sdkKeyCheck
    bx r3

sdkContinue:
    // The handler is entered with its address in r0, as the replaced bx r0 did.
    movs r0, r2
    bx r0

.balign 4

.global patch_ingamereset_sdkIrqTable
patch_ingamereset_sdkIrqTable:
    .word 0

.global patch_ingamereset_sdkIrqReturn
patch_ingamereset_sdkIrqReturn:
    .word 0

.global patch_ingamereset_sdkKeyCheck
patch_ingamereset_sdkKeyCheck:
    .word 0

.pool

.section "patch_ingamereset_dispatch_blx", "ax"

// Dispatch part for a dispatcher that picks the interrupt with a single clz and calls the
// handler with blx, found in Golden Sun: Dark Dawn. Its whole body is conditional on there
// being something to service:
//
//     mov   ip, #0x04000000
//     ldr   r2, [ip, #0x208]!        @ IME
//     ldrd  r0, r1, [ip, #8]!        @ IE, IF
//     cmp   r2, #0
//     andsne r1, r0, r1              @ pending = IE & IF
//     pushne {lr}
//     ldrne lr, =irqTableEnd
//     rsbne r0, r1, #0
//     andne r1, r0, r1               @ lowest pending bit
//     clzne r0, r1
//     ldrne lr, [lr, -r0, lsl #2]    @ handler = table[31 - clz]
//     strne r1, [ip, #4]             @ acknowledge
//     blx   lr
//
// It replaces the last two conditional instructions, so it is entered with the flags of
// "andsne r1, r0, r1" still set, r0 = clz, r1 = the lowest pending bit, ip = 0x04000210,
// and lr = the address just past the end of the handler table. There is no need for a copy
// of the table address, because lr already holds it.
//
// Unlike the stock dispatcher this one has not acknowledged the interrupt yet, so the
// acknowledging write is part of the replaced code and is done here.

.thumb
.global patch_ingamereset_blxDispatch
.type patch_ingamereset_blxDispatch, %function
patch_ingamereset_blxDispatch:
    // The jump into here does not change the flags, so they still say whether anything is
    // pending. Nothing is pending when IME is off, or when IE & IF is zero.
    bne blxPending
    // The replaced code would fall through to "blx lr" with lr still holding the return
    // into the BIOS, which never comes back: the matching "pushne {lr}" did not run on
    // this path, so returning here would unbalance the dispatcher's stack. bx lr is
    // therefore the same thing without the pointless return address.
    bx lr

blxPending:
    // handler = [lr - clz * 4]. Thumb can only subtract low registers, so lr is copied.
    mov r3, lr
    lsls r2, r0, #2
    subs r2, r3, r2
    ldr r2, [r2] // r2 = handler, which is where normal dispatch carries on

    // Acknowledge the interrupt, as the replaced "strne r1, [ip, #4]" did. REG_IF is at
    // 0x04000214 and ip holds 0x04000210. Thumb can only store through a low register.
    mov r3, ip
    str r1, [r3, #4]

    ldr r3, patch_ingamereset_blxIrqReturn
    mov lr, r3
    cmp r1, #1 // bit 0 is vblank, and only vblank advances the count
    bne blxContinue
    ldr r3, patch_ingamereset_blxKeyCheck
    bx r3

blxContinue:
    movs r0, r2
    bx r0

.balign 4

.global patch_ingamereset_blxIrqReturn
patch_ingamereset_blxIrqReturn:
    .word 0

.global patch_ingamereset_blxKeyCheck
patch_ingamereset_blxKeyCheck:
    .word 0

.pool

.section "patch_ingamereset_reset", "ax"

// Entered from the key check with r2 = the address that carries on with normal dispatch.
// Returns there when the reset cannot be performed yet.
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
