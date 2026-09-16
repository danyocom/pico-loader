#pragma once

/// @brief Value placed in the shared reset parameter by the in-game reset patch, telling
///        the reloaded Pico Loader to boot the launcher instead of restarting the game.
///        Games pass their own values to OS_ResetSystem, so this is chosen to be unlike
///        any small integer or pointer a game would use.
///        Stored little-endian, its bytes read "PIGR" (Pico In-Game Reset) in a memory dump.
#define IN_GAME_RESET_PARAM_RETURN_TO_LAUNCHER  0x52474950

/// @brief Argument handed to the launcher, after its own path, when it is booted by an
///        in-game reset rather than from power on. A launcher can use it to restore
///        state belonging to the session the reset interrupted, and one that has no use
///        for it simply ignores the extra argument.
#define IN_GAME_RESET_LAUNCHER_ARGUMENT         "igr"
