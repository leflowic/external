#pragma once
#include <cstdint>

// Mirrors SAMP_Internal/sdk.h and SAMP_Internal/global.cpp exactly (same game
// build: SA-MP 0.3.7 R1 / GTA SA US 1.0) - kept as plain offset constants here
// instead of the padded-union struct trick, since external reads go through
// ReadProcessMemory field-by-field rather than dereferencing local pointers.
namespace OFF {
    // gta_sa.exe-relative
    constexpr uint32_t CAMERA_PTR      = 0x1542F4; // deref once, then -0x4c to reach the camera struct base
    constexpr uint32_t CAMERA_ADJUST   = 0x4c;
    constexpr uint32_t OPTIONS_OPENED  = 0x76B964;
    constexpr uint32_t RADIO           = 0x4CB7A5;
    constexpr uint32_t WINDOW_SIZE     = 0x817044; // int width, then +4 = int height

    // samp.dll-relative
    constexpr uint32_t SAMP_PTR        = 0x21A0F8; // deref once -> stSamp*
    constexpr uint32_t CHATOPEN_PTR    = 0x21A10C; // deref once, +0x54 -> chat-open byte

    // camera
    constexpr uint32_t CAM_MODE   = 0x30;  // short
    constexpr uint32_t CAM_ALPHA  = 0xd0;  // float
    constexpr uint32_t CAM_FOV    = 0xd8;  // float
    constexpr uint32_t CAM_BETA   = 0xe0;  // float
    constexpr uint32_t CAM_POS    = 0x854; // Vector3

    // matrix (per-ped world transform)
    constexpr uint32_t MATRIX_POS = 0x30; // Vector3

    // actor_info (gtaPed)
    constexpr uint32_t ACTOR_MAX   = 0x14;  // matrix*
    constexpr uint32_t ACTOR_SPEED = 0x44;  // Vector3
    constexpr uint32_t ACTOR_STATE = 0x530; // uint32_t
    constexpr uint32_t ACTOR_HP    = 0x540; // float

    constexpr uint32_t ACTOR_STATE_DYING = 54;
    constexpr uint32_t ACTOR_STATE_DEAD  = 55;

    // ped
    constexpr uint32_t PED_GTAPED = 0x2a4; // actor_info*

    // localplayer
    constexpr uint32_t LOCALPLAYER_ACTOR   = 0x0;   // ped*
    constexpr uint32_t LOCALPLAYER_VEHICLE = 0x14;  // uint16_t
    constexpr uint32_t LOCALPLAYER_TEAMID  = 0x108; // uint8_t

    // remoteplayerdata
    constexpr uint32_t RPDATA_ACTOR  = 0x0;   // ped*
    constexpr uint32_t RPDATA_TEAMID = 0x8;   // uint8_t
    constexpr uint32_t RPDATA_HEALTH = 0x1bc; // float (unused externally - actor->gtaPed->hp is used instead, matching aimbot.cpp)

    // remoteplayer
    constexpr uint32_t RP_DATA = 0x0; // remoteplayerdata*

    // playerpool
    constexpr uint32_t POOL_MAXID       = 0x0;
    constexpr uint32_t POOL_LOCALID     = 0x4;
    constexpr uint32_t POOL_LOCALPLAYER = 0x22;  // localplayer*
    constexpr uint32_t POOL_REMOTEPTRS  = 0x2e;  // remoteplayer*[SAMP_MAX_PLAYERS]
    constexpr uint32_t POOL_ISLISTED    = 0xfde; // int[SAMP_MAX_PLAYERS]

    // pools
    constexpr uint32_t POOLS_PLAYERPOOL = 0x18; // playerpool*

    // stSamp
    constexpr uint32_t SAMP_GAMESTATE = 0x3bd;
    constexpr uint32_t SAMP_POOLS     = 0x3cd; // pools*
    constexpr uint32_t SAMP_GAMESTATE_CONNECTED = 14;

    constexpr int SAMP_MAX_PLAYERS = 1004;
}
