#pragma once
#include <Windows.h>

namespace NAMETAG {
    void init(HANDLE hProc, uintptr_t sampBase);
    void set(bool enable);
    bool isSafe(); // false if the live bytes didn't match what's expected - patch refused
    bool isEnabled();
    bool wasAutoFound(); // true if the reference offsets missed and the auto-scan found them instead
}
