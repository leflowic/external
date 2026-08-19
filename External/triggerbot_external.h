#pragma once
#include <Windows.h>

// Reads the game's own native "currently targeted actor" pointer (the same
// one the engine uses for lock-on) rather than approximating alignment via an
// FOV cone - this address is confirmed by two independent sources (a public
// CLEO script and a separate .NET external tool). Fires via a simulated left
// click (SendInput), matching how that .NET tool actually pulls the trigger.
namespace TRIGGERBOT {
    struct Config {
        bool enabled = false;
        DWORD key = VK_RBUTTON; // must be "aiming" for the native target field to be valid
        int holdMs = 100;       // minimum time between shots
        float fov = 2.5f;       // degrees - how precisely the crosshair must be on the locked target to fire
    };

    extern Config config;

    void init(HANDLE hProc, uintptr_t gtaBase, uintptr_t sampBase);
    void tick();
}
