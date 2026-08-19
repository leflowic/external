#include <Windows.h>
#include <d3d9.h>
#pragma comment(lib, "d3d9.lib")

#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_win32.h"
#include "ImGui/imgui_impl_dx9.h"

#include "mem_external.h"
#include "offsets.h"
#include "aimbot_external.h"
#include "nametag_external.h"
#include "triggerbot_external.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static HWND g_hwnd = nullptr;
static IDirect3D9Ex* g_d3d = nullptr;
static IDirect3DDevice9Ex* g_device = nullptr;
static D3DPRESENT_PARAMETERS g_presentParams = {};

static HANDLE g_hProc = nullptr;
static uintptr_t g_gtaBase = 0;
static uintptr_t g_sampBase = 0;

static DWORD g_nametagKey = VK_F3;
static bool g_nametagKeyHeld = false;

// Rebindable-key UI: click "Bind", then whatever key/mouse button you press
// next becomes the bind. Captures nothing on the frame the button is clicked
// (so the click itself never becomes the bind), then grabs the first key seen.
static DWORD* g_capturingKey = nullptr;
static bool g_captureSkipThisFrame = false;

static const char* GetKeyName(DWORD vk) {
    static char buf[64];
    switch (vk) {
    case 0: return "None";
    case VK_LBUTTON: return "Mouse Left";
    case VK_RBUTTON: return "Mouse Right";
    case VK_MBUTTON: return "Mouse Middle";
    case VK_XBUTTON1: return "Mouse 4";
    case VK_XBUTTON2: return "Mouse 5";
    }
    UINT scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    LONG lparam = (LONG)(scan << 16);
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
        vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT ||
        vk == VK_INSERT || vk == VK_DELETE) {
        lparam |= (1 << 24);
    }
    if (GetKeyNameTextA(lparam, buf, sizeof(buf)) > 0) return buf;
    sprintf_s(buf, "VK 0x%02X", vk);
    return buf;
}

static bool KeybindButton(const char* label, DWORD* key) {
    bool changed = false;
    ImGui::PushID(label);
    ImGui::Text("%s", label);
    ImGui::SameLine();

    bool isCapturingThis = (g_capturingKey == key);
    if (ImGui::Button(isCapturingThis ? "Press a key..." : GetKeyName(*key), ImVec2(140, 0))) {
        g_capturingKey = key;
        g_captureSkipThisFrame = true;
    }
    ImGui::PopID();
    return changed;
}

// Scans for any pressed key/mouse button and assigns it to *g_capturingKey.
// Call once per frame, after ImGui has processed its own input for the frame.
static void UpdateKeyCapture() {
    if (!g_capturingKey) return;

    if (g_captureSkipThisFrame) {
        g_captureSkipThisFrame = false;
        return;
    }

    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
        g_capturingKey = nullptr;
        return;
    }

    for (int vk = 1; vk < 255; ++vk) {
        if (vk == VK_ESCAPE) continue;
        if (GetAsyncKeyState(vk) & 0x8000) {
            *g_capturingKey = (DWORD)vk;
            g_capturingKey = nullptr;
            break;
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        if (g_device && wParam != SIZE_MINIMIZED) {
            g_presentParams.BackBufferWidth = LOWORD(lParam);
            g_presentParams.BackBufferHeight = HIWORD(lParam);
            ImGui_ImplDX9_InvalidateDeviceObjects();
            g_device->ResetEx(&g_presentParams, nullptr);
            ImGui_ImplDX9_CreateDeviceObjects();
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Direct3DCreate9Ex/CreateDeviceEx (Vista+) rather than the classic
// Direct3DCreate9/CreateDevice - the classic API fails with D3DERR_DEVICELOST
// the moment you try to create a second D3D9 device on the same adapter while
// GTA SA holds it in exclusive fullscreen mode. The Ex device model is what
// Microsoft added specifically so unrelated windowed D3D9 apps can coexist
// with an exclusive-fullscreen app on the same GPU.
static bool CreateDevice(HWND hwnd) {
    if (FAILED(Direct3DCreate9Ex(D3D_SDK_VERSION, &g_d3d))) return false;

    ZeroMemory(&g_presentParams, sizeof(g_presentParams));
    g_presentParams.Windowed = TRUE;
    g_presentParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_presentParams.BackBufferFormat = D3DFMT_UNKNOWN;
    g_presentParams.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    HRESULT hr = g_d3d->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_presentParams, nullptr, &g_device);

    if (FAILED(hr)) {
        printf("[!] CreateDeviceEx (HW) failed: 0x%08X\n", (unsigned)hr);
        hr = g_d3d->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &g_presentParams, nullptr, &g_device);
    }

    if (FAILED(hr)) {
        printf("[!] CreateDeviceEx (SW) failed: 0x%08X\n", (unsigned)hr);
        return false;
    }

    return true;
}

static void RenderFrame() {
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("External", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    ImGui::BeginTabBar("##tabs");

    if (ImGui::BeginTabItem("Aimbot")) {
        ImGui::Checkbox("Enabled", &AIMBOT::config.enabled);
        KeybindButton("Aimbot key", &AIMBOT::config.aimKey);
        ImGui::Checkbox("Dont aim at teammates", &AIMBOT::config.dontShootMates);
        ImGui::SliderFloat("Virtual height modifier", &AIMBOT::config.heightChange, -1, 1, "%+.3f");
        ImGui::SliderFloat("Aim FOV", &AIMBOT::config.fov, 0, 180);
        ImGui::Checkbox("Prediction", &AIMBOT::config.prediction);
        ImGui::SliderInt("Prediction level", (int*)&AIMBOT::config.predictionLvl, 0, 10);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Triggerbot")) {
        ImGui::Checkbox("Enabled", &TRIGGERBOT::config.enabled);
        KeybindButton("Trigger key", &TRIGGERBOT::config.key);
        ImGui::SliderInt("Min. time between shots (ms)", &TRIGGERBOT::config.holdMs, 20, 400);
        ImGui::SliderFloat("Precision FOV", &TRIGGERBOT::config.fov, 0.5f, 15.f, "%.1f");
        ImGui::TextDisabled("Fires while held, only when the crosshair is precisely on the locked target.");
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Misc")) {
        if (!NAMETAG::isSafe()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Unsupported samp.dll build - nametag patch disabled");
        } else {
            ImGui::Text("Nametags: %s", NAMETAG::isEnabled() ? "ON" : "OFF");
            if (NAMETAG::wasAutoFound()) {
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "(offsets located via auto-scan)");
            }
            KeybindButton("Toggle nametags key", &g_nametagKey);
        }
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
    ImGui::End();

    UpdateKeyCapture();

    ImGui::Render();

    g_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(20, 20, 20), 1.0f, 0);
    if (SUCCEEDED(g_device->BeginScene())) {
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        g_device->EndScene();
    }
    g_device->PresentEx(nullptr, nullptr, nullptr, nullptr, 0);
}

int main() {
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    printf("[*] Waiting for gta_sa.exe...\n");
    DWORD pid = 0;
    while (!pid) { pid = MEM::FindProcessId("gta_sa.exe"); if (!pid) Sleep(1000); }
    printf("[+] Found gta_sa.exe (PID %lu)\n", pid);

    g_hProc = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!g_hProc) {
        printf("[!] OpenProcess failed (%lu). Run as administrator.\n", GetLastError());
        system("pause");
        return 1;
    }

    printf("[*] Waiting for samp.dll...\n");
    while (!g_sampBase) { g_sampBase = MEM::FindModuleBase(pid, "samp.dll"); if (!g_sampBase) Sleep(1000); }
    g_gtaBase = MEM::FindModuleBase(pid, "gta_sa.exe");
    printf("[+] gta_sa.exe base: 0x%08X  samp.dll base: 0x%08X\n", (unsigned)g_gtaBase, (unsigned)g_sampBase);

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0, 0, hInstance, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, "ExternalWndClass", nullptr };
    RegisterClassEx(&wc);

    g_hwnd = CreateWindowEx(0, wc.lpszClassName, "SAMP_Internal - External",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 480, 420,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDevice(g_hwnd)) {
        printf("[!] Failed to create D3D9 device.\n");
        system("pause");
        return 1;
    }

    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX9_Init(g_device);

    NAMETAG::init(g_hProc, g_sampBase);
    if (!NAMETAG::isSafe()) {
        printf("[!] Nametag offsets don't match this samp.dll build, and the auto-scan couldn't\n");
        printf("    find them either - nametag patch disabled.\n");
    } else if (NAMETAG::wasAutoFound()) {
        printf("[+] Nametag offsets didn't match the reference build - auto-scan found them.\n");
    } else {
        printf("[+] Nametag offsets verified.\n");
    }
    AIMBOT::init(g_hProc, g_gtaBase, g_sampBase);
    TRIGGERBOT::init(g_hProc, g_gtaBase, g_sampBase);

    printf("[+] Ready. Aim key works globally, no need to switch to this window.\n");

    MSG msg;
    while (true) {
        DWORD exitCode = 0;
        if (!GetExitCodeProcess(g_hProc, &exitCode) || exitCode != STILL_ACTIVE) {
            printf("[*] Target process closed.\n");
            break;
        }

        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto shutdown;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!g_capturingKey && NAMETAG::isSafe()) {
            bool down = (GetAsyncKeyState(g_nametagKey) & 0x8000) != 0;
            if (down && !g_nametagKeyHeld) {
                NAMETAG::set(!NAMETAG::isEnabled());
            }
            g_nametagKeyHeld = down;
        }

        AIMBOT::tick();
        TRIGGERBOT::tick();
        RenderFrame();

        Sleep(1);
    }

shutdown:
    if (NAMETAG::isEnabled()) NAMETAG::set(false);

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (g_device) g_device->Release();
    if (g_d3d) g_d3d->Release();
    if (g_hProc) CloseHandle(g_hProc);

    return 0;
}
