#include "mem_external.h"

DWORD MEM::FindProcessId(const char* exeName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32 pe{ sizeof(pe) };
    DWORD pid = 0;
    if (Process32First(snap, &pe)) {
        do {
            if (!_stricmp(pe.szExeFile, exeName)) { pid = pe.th32ProcessID; break; }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

DWORD MEM::FindModuleBase(DWORD pid, const char* moduleName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    MODULEENTRY32 me{ sizeof(me) };
    DWORD base = 0;
    if (Module32First(snap, &me)) {
        do {
            if (!_stricmp(me.szModule, moduleName)) { base = (DWORD)(uintptr_t)me.modBaseAddr; break; }
        } while (Module32Next(snap, &me));
    }
    CloseHandle(snap);
    return base;
}

uintptr_t MEM::FindDMAAddy(HANDLE hProc, uintptr_t ptr, const std::vector<uint32_t>& offsets) {
    uintptr_t addr = ptr;
    for (size_t i = 0; i < offsets.size(); ++i) {
        addr = Read<uint32_t>(hProc, addr);
        addr += offsets[i];
    }
    return addr;
}
