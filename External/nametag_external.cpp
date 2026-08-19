#include "nametag_external.h"
#include "mem_external.h"
#include <vector>

// Known-good offsets for the samp.dll build this was last verified against.
// Used both as the first thing tried (fast path) and as the reference layout
// for the auto-scan fallback below.
constexpr uint32_t REF_ADDR1 = 0x6FCF3;
constexpr uint32_t REF_ADDR2 = 0x6FD14;
constexpr uint32_t REF_ADDR3 = 0x70E24;
constexpr uint32_t REF_ADDR4 = 0x6FE28;
constexpr uint32_t REF_ADDR5 = 0x70F38;

static const BYTE NOP6[6] = { 0x90,0x90,0x90,0x90,0x90,0x90 };
static const BYTE NOP2[2] = { 0x90,0x90 };

static HANDLE g_hProc = nullptr;
static uintptr_t g_base = 0;
static uint32_t g_addr1 = REF_ADDR1, g_addr2 = REF_ADDR2, g_addr3 = REF_ADDR3, g_addr4 = REF_ADDR4, g_addr5 = REF_ADDR5;
static BYTE g_orig1[6], g_orig2[6], g_orig3[6], g_orig4[2], g_orig5[2];
static bool g_captured = false;
static bool g_safe = false;
static bool g_enabled = false;
static bool g_wasAutoFound = false;

static bool WriteBytes(uintptr_t addr, const void* data, size_t size) {
    DWORD oldProtect;
    if (!VirtualProtectEx(g_hProc, (LPVOID)addr, size, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    SIZE_T written = 0;
    bool ok = WriteProcessMemory(g_hProc, (LPVOID)addr, data, size, &written) && written == size;

    DWORD ignored;
    VirtualProtectEx(g_hProc, (LPVOID)addr, size, oldProtect, &ignored);
    return ok;
}

// Verified against the actual samp.dll on disk: the 6-byte sites are Jcc near
// (0F 8x / rel32 - conditional near jump), not a call as first assumed.
static bool LooksLikeJccNear6(const BYTE* b) { return b[0] == 0x0F && (b[1] & 0xF0) == 0x80; }
static bool LooksLikeShortJump2(const BYTE* b) { return b[0] == 0xEB || (b[0] >= 0x70 && b[0] <= 0x7F); }

static bool CaptureAndVerify(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    BYTE b1[6], b2[6], b3[6], b4[2], b5[2];
    if (!ReadProcessMemory(g_hProc, (LPCVOID)(g_base + a1), b1, 6, nullptr)) return false;
    if (!ReadProcessMemory(g_hProc, (LPCVOID)(g_base + a2), b2, 6, nullptr)) return false;
    if (!ReadProcessMemory(g_hProc, (LPCVOID)(g_base + a3), b3, 6, nullptr)) return false;
    if (!ReadProcessMemory(g_hProc, (LPCVOID)(g_base + a4), b4, 2, nullptr)) return false;
    if (!ReadProcessMemory(g_hProc, (LPCVOID)(g_base + a5), b5, 2, nullptr)) return false;

    if (!(LooksLikeJccNear6(b1) && LooksLikeJccNear6(b2) && LooksLikeJccNear6(b3)
        && LooksLikeShortJump2(b4) && LooksLikeShortJump2(b5)))
        return false;

    g_addr1 = a1; g_addr2 = a2; g_addr3 = a3; g_addr4 = a4; g_addr5 = a5;
    memcpy(g_orig1, b1, 6); memcpy(g_orig2, b2, 6); memcpy(g_orig3, b3, 6);
    memcpy(g_orig4, b4, 2); memcpy(g_orig5, b5, 2);
    return true;
}

// ---------------------------------------------------------------------------
// Auto-scan fallback: if the reference offsets don't check out (a newer/older
// samp.dll build shifted the code around), search the module's own executable
// section for the same instruction shapes and recover the offsets instead of
// just giving up. Patterns keep the opcode/mnemonic-adjacent bytes fixed and
// wildcard out anything that encodes an address or displacement (those are
// exactly what changes between builds even when the surrounding logic didn't).
// A candidate is only trusted if site1's pattern is found AND sites 2-5 also
// match at the same relative offsets seen in the reference build - matching
// one pattern alone isn't enough signal, since short jump/opcode shapes like
// these are common and could coincidentally appear elsewhere in ~850KB of code.
// ---------------------------------------------------------------------------

struct Pattern {
    std::vector<BYTE> bytes;
    std::vector<bool> wildcard; // true = don't compare this byte
};

static Pattern MakePattern(std::initializer_list<int> spec) {
    // -1 in the spec means wildcard.
    Pattern p;
    for (int v : spec) {
        p.wildcard.push_back(v < 0);
        p.bytes.push_back(v < 0 ? 0 : (BYTE)v);
    }
    return p;
}

static bool MatchAt(const std::vector<BYTE>& buf, size_t pos, const Pattern& pat) {
    if (pos + pat.bytes.size() > buf.size()) return false;
    for (size_t i = 0; i < pat.bytes.size(); ++i) {
        if (!pat.wildcard[i] && buf[pos + i] != pat.bytes[i]) return false;
    }
    return true;
}

static std::vector<size_t> FindAll(const std::vector<BYTE>& buf, const Pattern& pat) {
    std::vector<size_t> hits;
    if (buf.size() < pat.bytes.size()) return hits;
    for (size_t i = 0; i <= buf.size() - pat.bytes.size(); ++i) {
        if (MatchAt(buf, i, pat)) hits.push_back(i);
    }
    return hits;
}

// Locates the executable section (first section with IMAGE_SCN_MEM_EXECUTE)
// of a PE module already mapped into the target process, without assuming a
// fixed size - so this keeps working even if a rebuild changes the section's
// virtual size somewhat.
static bool GetExecutableSection(uintptr_t moduleBase, uint32_t& outRva, uint32_t& outSize) {
    BYTE dosHeader[64];
    if (!ReadProcessMemory(g_hProc, (LPCVOID)moduleBase, dosHeader, sizeof(dosHeader), nullptr)) return false;
    if (dosHeader[0] != 'M' || dosHeader[1] != 'Z') return false;
    uint32_t e_lfanew = *(uint32_t*)(dosHeader + 0x3C);

    BYTE peHeader[24];
    if (!ReadProcessMemory(g_hProc, (LPCVOID)(moduleBase + e_lfanew), peHeader, sizeof(peHeader), nullptr)) return false;
    if (peHeader[0] != 'P' || peHeader[1] != 'E') return false;
    uint16_t numSections = *(uint16_t*)(peHeader + 6);
    uint16_t optHeaderSize = *(uint16_t*)(peHeader + 20);

    uint32_t sectionsOff = e_lfanew + 24 + optHeaderSize;
    for (uint16_t i = 0; i < numSections; ++i) {
        BYTE sec[40];
        if (!ReadProcessMemory(g_hProc, (LPCVOID)(moduleBase + sectionsOff + i * 40u), sec, sizeof(sec), nullptr)) return false;

        uint32_t characteristics = *(uint32_t*)(sec + 36);
        constexpr uint32_t SCN_MEM_EXECUTE = 0x20000000;
        if (characteristics & SCN_MEM_EXECUTE) {
            outRva = *(uint32_t*)(sec + 12);
            uint32_t virtSize = *(uint32_t*)(sec + 8);
            uint32_t rawSize = *(uint32_t*)(sec + 16);
            outSize = virtSize > rawSize ? virtSize : rawSize;
            return true;
        }
    }
    return false;
}

static bool AutoScan() {
    uint32_t textRva = 0, textSize = 0;
    if (!GetExecutableSection(g_base, textRva, textSize)) return false;
    if (textSize == 0 || textSize > 0x2000000) return false; // sanity cap

    std::vector<BYTE> buf(textSize);
    SIZE_T totalRead = 0;
    // Large regions can be partially unmapped/paged oddly; read in chunks and
    // tolerate the tail failing rather than aborting the whole scan.
    constexpr SIZE_T CHUNK = 0x10000;
    for (SIZE_T off = 0; off < textSize; off += CHUNK) {
        SIZE_T size = min(CHUNK, textSize - off);
        SIZE_T read = 0;
        if (ReadProcessMemory(g_hProc, (LPCVOID)(g_base + textRva + off), buf.data() + off, size, &read)) {
            totalRead += read;
        }
    }
    if (totalRead < textSize / 2) return false; // too much missing to trust

    // Reference deltas (site N relative to site 1) from the known-good build.
    const uint32_t delta2 = REF_ADDR2 - REF_ADDR1;
    const uint32_t delta3 = REF_ADDR3 - REF_ADDR1;
    const uint32_t delta4 = REF_ADDR4 - REF_ADDR1;
    const uint32_t delta5 = REF_ADDR5 - REF_ADDR1;

    // Site 1: JE near ; MOV EDI,EDI ; CALL rel32 ; MOV ECX,[abs32]
    Pattern pat1 = MakePattern({ 0x0F,0x84, -1,-1,-1,-1, 0x8B,0xCF, 0xE8, -1,-1,-1,-1, 0x8B,0x0D });
    // Site 2 / Site 3 share the same shape: Jcc near ; CMP BYTE [ESI+9],13 ; JNZ ; MOV ECX,[ESI+4]
    Pattern pat23 = MakePattern({ 0x0F,-1, -1,-1,-1,-1, 0x80,0x7E,0x09,0x13,0x75,0x41,0x8B,0x4E,0x04 });
    // Site 4: JZ short ; MOV EAX,[ESI+0Bh] ; TEST EAX,EAX ; JNZ
    Pattern pat4 = MakePattern({ 0x74,-1, 0x8B,0x46,0x0B, 0x85,0xC0, 0x75 });
    // Site 5: JZ short ; PUSH EDI ; PUSH EDI ; MOV ECX,EBX ; CALL rel32
    Pattern pat5 = MakePattern({ 0x74,-1, 0x57,0x57, 0x8B,0xCB, 0xE8, -1,-1,-1,-1 });

    std::vector<size_t> site1Hits = FindAll(buf, pat1);

    for (size_t hit : site1Hits) {
        uint32_t candidate1 = textRva + (uint32_t)hit;

        auto tryDelta = [&](uint32_t delta, const Pattern& pat) -> bool {
            size_t pos = hit + delta;
            return MatchAt(buf, pos, pat);
        };

        if (!tryDelta(delta2, pat23)) continue;
        if (!tryDelta(delta3, pat23)) continue;
        if (!tryDelta(delta4, pat4)) continue;
        if (!tryDelta(delta5, pat5)) continue;

        // All five line up at the expected relative layout - confirm by
        // capturing+verifying against the live process (belt and suspenders).
        if (CaptureAndVerify(candidate1, candidate1 + delta2, candidate1 + delta3, candidate1 + delta4, candidate1 + delta5)) {
            return true;
        }
    }

    return false;
}

void NAMETAG::init(HANDLE hProc, uintptr_t sampBase) {
    g_hProc = hProc;
    g_base = sampBase;
    g_captured = false;
    g_safe = false;
    g_enabled = false;
    g_wasAutoFound = false;

    if (CaptureAndVerify(REF_ADDR1, REF_ADDR2, REF_ADDR3, REF_ADDR4, REF_ADDR5)) {
        g_captured = true;
        g_safe = true;
        return;
    }

    if (AutoScan()) {
        g_captured = true;
        g_safe = true;
        g_wasAutoFound = true;
    }
}

void NAMETAG::set(bool enable) {
    if (!g_captured || !g_safe) return;

    if (enable) {
        WriteBytes(g_base + g_addr1, NOP6, 6);
        WriteBytes(g_base + g_addr2, NOP6, 6);
        WriteBytes(g_base + g_addr3, NOP6, 6);
        WriteBytes(g_base + g_addr4, NOP2, 2);
        WriteBytes(g_base + g_addr5, NOP2, 2);
    } else {
        WriteBytes(g_base + g_addr1, g_orig1, 6);
        WriteBytes(g_base + g_addr2, g_orig2, 6);
        WriteBytes(g_base + g_addr3, g_orig3, 6);
        WriteBytes(g_base + g_addr4, g_orig4, 2);
        WriteBytes(g_base + g_addr5, g_orig5, 2);
    }
    g_enabled = enable;
}

bool NAMETAG::isSafe() { return g_safe; }
bool NAMETAG::isEnabled() { return g_enabled; }
bool NAMETAG::wasAutoFound() { return g_wasAutoFound; }
