#include "common.h"
#include <memory>
#include <string.h>
#include <libtwl/ipc/ipcFifo.h>
#include <libtwl/ipc/ipcSync.h>
#include <libtwl/i2c/i2cMcu.h>
#include <libtwl/sio/sioRtc.h>
#include <libtwl/sound/sound.h>
#include <libtwl/sound/soundChannel.h>
#include <libtwl/sound/soundCapture.h>
#include "core/Environment.h"
#include "logger/NitroEmulatorOutputStream.h"
#include "logger/PicoAgbAdapterOutputStream.h"
#include "logger/NocashOutputStream.h"
#include "logger/NullLogger.h"
#include "logger/PlainLogger.h"
#include "fat/dldi.h"
#include "loader/NdsLoader.h"
#include "sharedMemory.h"
#include "ndsHeader.h"
#include "globalHeap.h"
#include "mmc/tmio.h"

#define HANDSHAKE_PART0     0xA
#define HANDSHAKE_PART1     0xB
#define HANDSHAKE_PART2     0xC
#define HANDSHAKE_PART3     0xD

ILogger* gLogger;
FATFS gFatFs;

static NdsLoader sLoader;

static void initLogger()
{
    std::unique_ptr<IOutputStream> outputStream;
    if (Environment::IsIsNitroEmulator() && Environment::SupportsAgbSemihosting())
    {
        outputStream = std::make_unique<NitroEmulatorOutputStream>();
    }
    // else if (Environment::HasPicoAgbAdapter())
    // {
    //     outputStream = std::make_unique<PicoAgbAdapterOutputStream>();
    // }
    else if (Environment::SupportsNocashPrint())
    { 
        outputStream = std::make_unique<NocashOutputStream>();
    }
    else
    {
        gLogger = new NullLogger();
        return;
    }
    gLogger = new PlainLogger(LogLevel::All, std::move(outputStream));
}

static bool mountDldi()
{
    FRESULT res = f_mount(&gFatFs, "fat:", 1);
    if (res != FR_OK)
    {
        LOG_ERROR("dldi mount failed: %d\n", res);
        return false;
    }
    f_chdrive("fat:");
    return true;
}

static bool mountDsiSd()
{
    FRESULT res = f_mount(&gFatFs, "sd:", 1);
    if (res != FR_OK)
    {
        LOG_ERROR("dsi sd mount failed: %d\n", res);
        return false;
    }
    f_chdrive("sd:");
    return true;
}

static bool mountAgbSemihosting()
{
    FRESULT res = f_mount(&gFatFs, "pc2:", 1);
    if (res != FR_OK)
    {
        LOG_ERROR("pc2 sd mount failed: %d\n", res);
        return false;
    }
    f_chdrive("pc2:");
    return true;
}

extern "C" void __libc_init_array();

static void handleSavePath()
{
    if (gLoaderHeader.loadParams.savePath[0] == 0)
    {
        char* savePath = (char*)gLoaderHeader.loadParams.savePath;
        strcpy(savePath, gLoaderHeader.loadParams.romPath);
        char* extension = strrchr(savePath, '.');
        if (!extension)
            extension = &savePath[strlen(savePath)];
        extension[0] = '.';
        extension[1] = 's';
        extension[2] = 'a';
        extension[3] = 'v';
        extension[4] = 0;
    }
    sLoader.SetSavePath(gLoaderHeader.loadParams.savePath);
}

static void clearSoundRegisters()
{
    REG_SOUNDCNT = 0;
    REG_SNDCAP0CNT = 0;
    REG_SNDCAP1CNT = 0;

    for (int i = 0; i < 16; i++)
    {
        REG_SOUNDxCNT(i) = 0;
        REG_SOUNDxSAD(i) = 0;
        REG_SOUNDxTMR(i) = 0;
        REG_SOUNDxPNT(i) = 0;
        REG_SOUNDxLEN(i) = 0;
    }
}

static void initIpc()
{
    ipc_clearSendFifo();
    ipc_ackFifoError();
    ipc_disableRecvFifoNotEmptyIrq();
    ipc_enableFifo();

    while (!ipc_isRecvFifoEmpty())
    {
        ipc_recvWordDirect();
    }

    ipc_setArm7SyncBits(HANDSHAKE_PART0);
    while (ipc_getArm9SyncBits() != HANDSHAKE_PART0);
    ipc_setArm7SyncBits(HANDSHAKE_PART1);
    while (ipc_getArm9SyncBits() != HANDSHAKE_PART1);
    ipc_setArm7SyncBits(HANDSHAKE_PART2);
    while (ipc_getArm9SyncBits() != HANDSHAKE_PART2);
    ipc_setArm7SyncBits(HANDSHAKE_PART3);
    while (ipc_getArm9SyncBits() == HANDSHAKE_PART2);
}

namespace
{
    // Bit for D-Pad Down in the (active-low) KEYINPUT register.
    constexpr u16 KEY_DOWN_BIT = 1 << 7;

    // There is no REG_KEYINPUT available to us on the arm7 side (unlike the
    // arm9 side, which gets it from <nds.h>), so it's read directly here.
    inline u16 ReadKeysHeld()
    {
        // KEYINPUT is active-low: a held button reads as a 0 bit.
        return ~(*(volatile u16*)0x04000130);
    }

    // IMPORTANT / KNOWN LIMITATION:
    // This only runs once the SDK's own reset handler has already detected
    // the classic START+SELECT+L+R combo and triggered a console-level soft
    // reset that lands us back here (arm7EntryAddress == our own entry
    // point). It does not detect or alter that combo, and it never runs at
    // all for games that don't invoke the standard NitroSDK OS_ResetSystem
    // for their soft reset (e.g. Mario Kart DS, which implements its own
    // reset handling). A fully general IGR - one that works regardless of
    // how a given game's binary implements reset - needs to hook into
    // something closer to universal, such as the arm7 OSi_IrqVBlank handler
    // already targeted by the cheat engine patch (see
    // arm9/source/patches/arm7/cheats/CheatEnginePatch.cpp). That requires
    // hand-verified, disassembly-checked injected machine code and is out
    // of scope for this change; this function only improves the reliability
    // of the check for the subset of games this mechanism already covers.
    //
    // Deliberately a plain iteration-count busy-wait rather than an ARM7
    // hardware timer.
    //
    // A wall-clock window via timer 3 (REG_TM3CNT_L/H) looks like the more
    // precise choice, but it is not sound here: this runs during a narrow
    // window around a soft reset, where timer state is whatever the resetting
    // game left behind, and reprogramming it can interfere with code still
    // using it. The window this loop guards only needs to be long enough to
    // sample the key state, not accurate.
    // keeps that proven mechanism, adding only a debounce requirement
    // (several consecutive positive samples, not just one) on top of it.
    bool WasComboHeldForIgr(u16 keyMask)
    {
        constexpr int WINDOW_ITERATIONS = 3000000; // same window as the original PR/diff
        constexpr int REQUIRED_CONSECUTIVE_HITS = 4;

        int consecutiveHits = 0;
        for (int i = 0; i < WINDOW_ITERATIONS; i++)
        {
            if ((ReadKeysHeld() & keyMask) == keyMask)
            {
                consecutiveHits++;
                if (consecutiveHits >= REQUIRED_CONSECUTIVE_HITS)
                {
                    return true;
                }
            }
            else
            {
                consecutiveHits = 0;
            }
        }

        return false;
    }
}

extern "C" void loaderMain()
{
    __libc_init_array();

    clearSoundRegisters();
    rtos_initIrq();
    rtos_startMainThread();
    initIpc();

    bool dsiMode = ipc_getArm9SyncBits() == 1;

    Environment::Initialize(dsiMode);
    heap_init();
    initLogger();

    rtc_init(); // ensure rtc irqs are disabled

    LOG_DEBUG("Pico Loader ARM7 started\n");

    if (Environment::IsDsiMode())
    {
        // Let the mcu handle the power button
        mcu_writeReg(MCU_REG_MODE, 0);
        TMIO_init();
    }

    memset(&gFatFs, 0, sizeof(gFatFs));
    bool multiboot = (gLoaderHeader.bootDrive & PLOAD_BOOT_DRIVE_MULTIBOOT_FLAG) != 0;
    gLoaderHeader.bootDrive &= ~PLOAD_BOOT_DRIVE_MULTIBOOT_FLAG;

    // Whether the ARM7 CPU is re-entering this same binary because a game's
    // SDK-triggered soft reset just happened (see the IGR branch further
    // down for the full explanation of why this check works).
    //
    // gLoaderHeader.dldiDriver is deliberately left alone here.
    //
    // Nulling it to force the "no valid driver" path looks defensible - the
    // pointer is old by this point - but it is not sound: the handed-down
    // driver is the one known to work in this boot, and discarding it trades
    // it for a rebuilt one for no benefit. The field is only ever written by
    // the external chainloader before this binary's _start, so there is no
    // mechanism by which it goes stale. Leaving it lets dldi_init() prefer
    // that fully-relocatable driver when one exists, and build its own only
    // when there genuinely is none - a case it detects by validating the
    // header, not by assuming.
    bool isReturningFromReset = !multiboot &&
        ((nds_header_ntr_t*)TWL_SHARED_MEMORY->ntrSharedMem.romHeader)->arm7EntryAddress == (u32)gLoaderHeader.entryPoint;

    switch (gLoaderHeader.bootDrive)
    {
        case PLOAD_BOOT_DRIVE_DLDI:
        {
            if (dldi_init())
            {
                mountDldi();
            }
            break;
        }
        case PLOAD_BOOT_DRIVE_DSI_SD:
        {
            if (Environment::IsDsiMode())
            {
                mountDsiSd();
            }
            break;
        }
        case PLOAD_BOOT_DRIVE_AGB_SEMIHOSTING:
        {
            if (Environment::SupportsAgbSemihosting())
            {
                mountAgbSemihosting();
            }
            break;
        }
    }

    if (gLoaderHeader.v3.cheats != nullptr && gLoaderHeader.v3.cheats->numberOfCheats != 0)
    {
        // Copy cheats to vram
        auto cheats = (pload_cheats_t*)malloc(gLoaderHeader.v3.cheats->length);
        memcpy(cheats, gLoaderHeader.v3.cheats, gLoaderHeader.v3.cheats->length);
        sLoader.SetCheats(cheats);
    }

    if (multiboot)
    {
        LOG_DEBUG("Multiboot\n");
        sLoader.Load(BootMode::Multiboot);
    }
    else if (isReturningFromReset)
    {
        // Where to boot on in-game reset.
        //
        // Prefer the path a chainloader configured in
        // gLoaderHeader.v2.launcherPath. That field is documented as optional
        // and is commonly left empty, and nothing on this path repopulates it,
        // so fall back to the well-known filename - otherwise in-game reset
        // would have no target at all on setups that never populate it.
        const char* launcherPath =
            (gLoaderHeader.v2.launcherPath[0] != 0)
                ? gLoaderHeader.v2.launcherPath
                : "/_picoboot.nds";
        bool igrRequested = WasComboHeldForIgr(KEY_DOWN_BIT);
        LOG_DEBUG("IGR check: keyMask held = %d, launcherPath = %s\n", (int)igrRequested, launcherPath);

        if (igrRequested)
        {
            LOG_DEBUG("IGR: loading launcher\n");
            sLoader.SetRomPath(launcherPath);

            // Record where the launcher lives, exactly as the normal load path
            // does. Without this, anything reached through an in-game reset has
            // no return path stored, so a homebrew application that offers a
            // return-to-launcher option has nowhere to go.
            sLoader.SetLauncherPath(launcherPath);

            sLoader.Load(BootMode::Normal);
        }
        else
        {
            LOG_DEBUG("Retail soft reset detected\n");
            u32 originalArm7EntryAddress = ((nds_header_ntr_t*)TWL_SHARED_MEMORY->ntrSharedMem.cardRomHeader)->arm7EntryAddress;
            ((nds_header_ntr_t*)TWL_SHARED_MEMORY->ntrSharedMem.romHeader)->arm7EntryAddress = originalArm7EntryAddress;
            sLoader.Load(BootMode::SdkResetSystem);
        }
    }
    else
    {
        sLoader.SetRomPath(gLoaderHeader.loadParams.romPath);
        handleSavePath();
        sLoader.SetArguments(gLoaderHeader.loadParams.arguments, gLoaderHeader.loadParams.argumentsLength);
        sLoader.SetLauncherPath(gLoaderHeader.v2.launcherPath);
        sLoader.Load(BootMode::Normal);
    }

    while (true);
}