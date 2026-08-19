#pragma once
#include <Windows.h>
#include "vector.h"

// Same silent-aim design as SAMP_Internal/aimbot.cpp: no mouse movement at
// all, just overwrites the camera's view angles (alpha/beta) directly in the
// target process's memory - trivial to do externally via WriteProcessMemory,
// no mouse simulation or timing tricks needed.
namespace AIMBOT {
    struct Config {
        bool enabled = false;
        DWORD aimKey = VK_LMENU;
        bool dontShootMates = false;
        float heightChange = 0.f;
        float fov = 180.f;
        bool prediction = false;
        unsigned int predictionLvl = 6;
    };

    extern Config config;

    void init(HANDLE hProc, uintptr_t gtaBase, uintptr_t sampBase);
    void tick(); // call every frame; internally checks config.enabled + aimKey
}
