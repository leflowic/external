#pragma once
#include <Windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <vector>

// All reads/writes go through the target process handle via ReadProcessMemory /
// WriteProcessMemory - this project never injects code into gta_sa.exe, so it
// never touches the game's own D3D9 device or DirectInput state, sidestepping
// the whole class of alt-tab/device-reset/cursor-acquisition issues the
// in-process DLL version ran into.
namespace MEM {
    DWORD FindProcessId(const char* exeName);
    DWORD FindModuleBase(DWORD pid, const char* moduleName);

    template <typename T>
    T Read(HANDLE hProc, uintptr_t addr) {
        T value{};
        ReadProcessMemory(hProc, (LPCVOID)addr, &value, sizeof(T), nullptr);
        return value;
    }

    template <typename T>
    bool Write(HANDLE hProc, uintptr_t addr, const T& value) {
        SIZE_T written = 0;
        return WriteProcessMemory(hProc, (LPVOID)addr, &value, sizeof(T), &written) && written == sizeof(T);
    }

    // Follows a chain of pointers: reads a 4-byte pointer at `ptr`, adds the
    // first offset, reads the pointer there, adds the next offset, etc.
    uintptr_t FindDMAAddy(HANDLE hProc, uintptr_t ptr, const std::vector<uint32_t>& offsets);
}
