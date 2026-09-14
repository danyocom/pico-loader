#pragma once

/// @brief Value placed in the shared reset parameter by the in-game reset patch, telling
///        the reloaded Pico Loader to boot the launcher instead of restarting the game.
///        Games pass their own values to OS_ResetSystem, so this is chosen to be unlike
///        any small integer or pointer a game would use.
///        Stored little-endian, its bytes read "PIGR" (Pico In-Game Reset) in a memory dump.
#define IN_GAME_RESET_PARAM_RETURN_TO_LAUNCHER  0x52474950
