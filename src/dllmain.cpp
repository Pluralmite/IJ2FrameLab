// IJ2FrameLab - live frame meter and hit/hurt shape overlay for Injustice 2 (PC)
// Loaded by Ultimate ASI Loader (dinput8.dll) from Binaries\Retail.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include "MinHook.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "config.h"
#include "game.h"
#include "ui.h"
#include "screenshot.h"
#include "log.h"
#include "ij2.h"
#include "trace.h"
#include "bot.h"
#include "movelist.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

static PresentFn g_origPresent = nullptr;
static ResizeBuffersFn g_origResize = nullptr;
static WNDPROC g_origWndProc = nullptr;
static HWND g_hwnd = nullptr;
static ID3D11Device* g_dev = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static bool g_imguiReady = false;
static HMODULE g_self = nullptr;
static std::string g_iniDir;
static int g_generation = 0;           // how many times the loader has (re)loaded this core
static volatile bool g_stopping = false;

// ---- keep the game "focused" while a bot script runs, so tests work with the window in the background ----
using HwndFn = HWND(WINAPI*)();
static HwndFn o_GetForegroundWindow = nullptr, o_GetActiveWindow = nullptr, o_GetFocus = nullptr;
static HWND WINAPI h_GetForegroundWindow() { return (bot::Running() && g_hwnd) ? g_hwnd : o_GetForegroundWindow(); }
static HWND WINAPI h_GetActiveWindow() { return (bot::Running() && g_hwnd) ? g_hwnd : o_GetActiveWindow(); }
static HWND WINAPI h_GetFocus() { return (bot::Running() && g_hwnd) ? g_hwnd : o_GetFocus(); }
static void FocusSpoofTick() {
    static bool was = false;
    bool now = bot::Running();
    if (now == was || !g_hwnd) return;
    was = now;
    bool reallyFg = o_GetForegroundWindow && o_GetForegroundWindow() == g_hwnd;
    if (reallyFg) return;
    // tell the game it is active (bot starts) / inactive again (bot ends)
    PostMessageW(g_hwnd, WM_ACTIVATEAPP, now ? TRUE : FALSE, 0);
    PostMessageW(g_hwnd, WM_ACTIVATE, now ? WA_ACTIVE : WA_INACTIVE, 0);
    PostMessageW(g_hwnd, now ? WM_SETFOCUS : WM_KILLFOCUS, 0, 0);
    Log("bot focus spoof %s", now ? "on" : "off");
}

static LRESULT CALLBACK HookedWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (g_stopping) return CallWindowProcW(g_origWndProc, h, msg, w, l);
    if (bot::Running()) {
        // while the bot drives the game, don't let it see that it lost focus
        if ((msg == WM_ACTIVATEAPP && !w) || (msg == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE) || msg == WM_KILLFOCUS) return 0;
    }
    static int kbLogged = 0;
    if (msg == WM_KEYDOWN && kbLogged < 3 && w != VK_F1 && (w < VK_F1 || w > VK_F12)) { kbLogged++; Log("input: keyboard key down %02X", (unsigned)w); }
    // FRAME LAB menu open: keyboard presses are for it, not for the game (key-ups still pass)
    if (g_imguiReady && UiCapturesInput() && (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_CHAR)) {
        ImGui_ImplWin32_WndProcHandler(h, msg, w, l);
        return 0;
    }
    if (g_imguiReady && (g_ui.menuOpen || g_ui.gameMenu)) {
        ImGui_ImplWin32_WndProcHandler(h, msg, w, l);
        ImGuiIO& io = ImGui::GetIO();
        // Swallow mouse / keyboard only while an ImGui widget actually wants it.
        if (io.WantCaptureMouse && (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST)) return 1;
        if (io.WantTextInput && (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_CHAR)) return 1;
    }
    return CallWindowProcW(g_origWndProc, h, msg, w, l);
}

static void CreateRTV(IDXGISwapChain* sc) {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back)) && back) {
        g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

static bool InitImGui(IDXGISwapChain* sc) {
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&g_dev))) return false;
    g_dev->GetImmediateContext(&g_ctx);
    DXGI_SWAP_CHAIN_DESC d{};
    sc->GetDesc(&d);
    g_hwnd = d.OutputWindow;
    CreateRTV(sc);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    static std::string imguiIni = g_iniDir + "IJ2FrameLab_layout.ini";
    io.IniFilename = imguiIni.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.FontGlobalScale = g_cfg.menuScale;
    ImGui::StyleColorsDark();
    {
        // the game's UI uses a clean DIN-like sans: Bahnschrift is the closest stock Windows font
        const char* sans[] = {"C:\\Windows\\Fonts\\bahnschrift.ttf", "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arial.ttf"};
        for (const char* c : sans)
            if (GetFileAttributesA(c) != INVALID_FILE_ATTRIBUTES) {
                g_uiFont = io.Fonts->AddFontFromFileTTF(c, 17.f);   // first font added = ImGui default
                g_valueFont = io.Fonts->AddFontFromFileTTF(c, 30.f);
                g_menuFont = io.Fonts->AddFontFromFileTTF(c, 48.f);
                break;
            }
        if (!g_uiFont) io.Fonts->AddFontDefault();
    }
    {
        // serif caps font for the frame data HUD (closest stock Windows match to the game's font)
        const char* candidates[] = {"C:\\Windows\\Fonts\\GOUDOS.TTF", "C:\\Windows\\Fonts\\georgia.ttf", "C:\\Windows\\Fonts\\times.ttf"};
        for (const char* c : candidates)
            if (GetFileAttributesA(c) != INVALID_FILE_ATTRIBUTES) { g_hudFont = io.Fonts->AddFontFromFileTTF(c, 40.f); break; }
    }
    ImGuiStyle& st = ImGui::GetStyle();
    // Injustice 2 menu look: dark navy panels, steel-blue headers, cyan highlights, square corners
    st.WindowRounding = 0.f;
    st.FrameRounding = 2.f;
    st.GrabRounding = 2.f;
    st.TabRounding = 0.f;
    st.WindowBorderSize = 1.f;
    st.FrameBorderSize = 0.f;
    st.WindowPadding = ImVec2(12, 10);
    st.ItemSpacing = ImVec2(8, 6);
    ImVec4* c = st.Colors;
    auto rgb = [](int r, int g, int b, float a = 1.f) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); };
    c[ImGuiCol_Text] = rgb(228, 236, 244);
    c[ImGuiCol_TextDisabled] = rgb(128, 156, 182);
    c[ImGuiCol_WindowBg] = rgb(22, 30, 40, 0.94f);
    c[ImGuiCol_PopupBg] = rgb(22, 30, 40, 0.97f);
    c[ImGuiCol_Border] = rgb(70, 102, 128, 0.9f);
    c[ImGuiCol_FrameBg] = rgb(36, 50, 66);
    c[ImGuiCol_FrameBgHovered] = rgb(52, 74, 96);
    c[ImGuiCol_FrameBgActive] = rgb(62, 96, 121);
    c[ImGuiCol_TitleBg] = rgb(62, 96, 121);
    c[ImGuiCol_TitleBgActive] = rgb(70, 108, 136);
    c[ImGuiCol_TitleBgCollapsed] = rgb(40, 60, 78);
    c[ImGuiCol_Header] = rgb(62, 96, 121);
    c[ImGuiCol_HeaderHovered] = rgb(86, 128, 160);
    c[ImGuiCol_HeaderActive] = rgb(64, 200, 236);
    c[ImGuiCol_Button] = rgb(52, 78, 102);
    c[ImGuiCol_ButtonHovered] = rgb(76, 116, 148);
    c[ImGuiCol_ButtonActive] = rgb(64, 200, 236);
    c[ImGuiCol_CheckMark] = rgb(80, 220, 248);
    c[ImGuiCol_SliderGrab] = rgb(80, 200, 236);
    c[ImGuiCol_SliderGrabActive] = rgb(120, 230, 252);
    c[ImGuiCol_Separator] = rgb(70, 102, 128);
    c[ImGuiCol_Tab] = rgb(40, 60, 78);
    c[ImGuiCol_TabHovered] = rgb(86, 128, 160);
    c[ImGuiCol_TabActive] = rgb(62, 96, 121);
    c[ImGuiCol_ResizeGrip] = rgb(62, 96, 121, 0.6f);
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    g_origWndProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
    g_imguiReady = true;
    g_ui.menuOpen = g_cfg.startVisible;
    Log("ImGui initialised (hwnd %p, %ux%u)", g_hwnd, d.BufferDesc.Width, d.BufferDesc.Height);
    return true;
}

static HRESULT WINAPI HookedPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (g_stopping) return g_origPresent(sc, sync, flags);
    if (!g_imguiReady) {
        if (!InitImGui(sc)) return g_origPresent(sc, sync, flags);
    }
    if (!g_rtv) CreateRTV(sc);

    UiHotkeys();
    bot::PollCommandFile(g_iniDir);
    bot::OnRenderFrame();
    FocusSpoofTick();
    GameTick();

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = (g_ui.menuOpen || g_ui.gameMenu) && !shot::HidingMenus();
    UiDraw(io.DisplaySize.x, io.DisplaySize.y);
    ImGui::Render();

    if (g_rtv) {
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
    shot::OnPresent(sc, g_dev, g_ctx);
    return g_origPresent(sc, sync, flags);
}

static HRESULT WINAPI HookedResize(IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl) {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    if (g_imguiReady) ImGui_ImplDX11_InvalidateDeviceObjects();
    HRESULT hr = g_origResize(sc, n, w, h, f, fl);
    if (g_imguiReady) ImGui_ImplDX11_CreateDeviceObjects();
    return hr;
}

// Build a throwaway device + swap chain just to read the DXGI vtable.
static bool GetSwapChainVTable(void**& vtbl) {
    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, DefWindowProcW, 0, 0, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr,
                   L"IJ2FrameLabDummy", nullptr};
    RegisterClassExW(&wc);
    HWND hw = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hw;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd,
                                               &sc, &dev, &fl, &ctx);
    if (FAILED(hr))
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc,
                                           &dev, &fl, &ctx);
    bool ok = SUCCEEDED(hr) && sc;
    static void* copy[32];
    if (ok) {
        void** v = *(void***)sc;
        for (int i = 0; i < 32; ++i) copy[i] = v[i];
        vtbl = copy;
    }
    if (sc) sc->Release();
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    DestroyWindow(hw);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

static DWORD WINAPI InitThread(LPVOID) {
    // Paths: everything lives next to the .asi
    char path[MAX_PATH];
    GetModuleFileNameA(g_self, path, MAX_PATH);
    std::string p = path;
    g_iniDir = p.substr(0, p.find_last_of("\\/") + 1);
    LogInit(g_iniDir + "IJ2FrameLab.log", g_generation > 0);
    Log("IJ2FrameLab core loading from %s (load #%d, build %s %s)", p.c_str(), g_generation + 1, __DATE__, __TIME__);
    g_cfg.Load(g_iniDir + "IJ2FrameLab.ini");
    g_cfg.baseDir = g_iniDir;
    ij2::Init();

    // Give the game time to create its own device first (not needed on a hot reload).
    for (int i = 0; i < 100 && !GetModuleHandleW(L"d3d11.dll"); ++i) Sleep(100);
    if (g_generation == 0) Sleep(1500);

    void** vt = nullptr;
    if (!GetSwapChainVTable(vt)) { Log("Failed to create dummy D3D11 device - overlay disabled"); return 0; }
    if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) { Log("MinHook init failed"); return 0; }
    if (MH_CreateHook(vt[8], (void*)HookedPresent, (void**)&g_origPresent) != MH_OK ||
        MH_CreateHook(vt[13], (void*)HookedResize, (void**)&g_origResize) != MH_OK) {
        Log("Failed to create DXGI hooks");
        return 0;
    }
    MH_CreateHookApi(L"user32", "GetForegroundWindow", (LPVOID)h_GetForegroundWindow, (LPVOID*)&o_GetForegroundWindow);
    MH_CreateHookApi(L"user32", "GetActiveWindow", (LPVOID)h_GetActiveWindow, (LPVOID*)&o_GetActiveWindow);
    MH_CreateHookApi(L"user32", "GetFocus", (LPVOID)h_GetFocus, (LPVOID*)&o_GetFocus);
    MH_EnableHook(MH_ALL_HOOKS);
    Log("DXGI Present/ResizeBuffers hooked");
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Hot reload: IJ2FrameLab.asi (the loader) loads a copy of this DLL and calls these exports.
// ---------------------------------------------------------------------------------------------
extern "C" __declspec(dllexport) void CoreStart(int generation) {
    g_generation = generation;
    HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
}

extern "C" __declspec(dllexport) int CoreWantsReload() { return g_reloadRequested ? 1 : 0; }

// Called from the loader's thread. Returns 0 if it is not safe to unload.
extern "C" __declspec(dllexport) int CoreStop() {
    Log("hot reload: stopping");
    if (g_hwnd && g_origWndProc && (WNDPROC)GetWindowLongPtrW(g_hwnd, GWLP_WNDPROC) != HookedWndProc) {
        Log("hot reload: another module subclassed the game window after us - cannot unload safely");
        g_reloadRequested = false;
        return 0;
    }
    ij2::SetFrozen(false);
    g_stopping = true;                     // hooks become pass-through immediately
    MH_DisableHook(MH_ALL_HOOKS);
    Sleep(300);                            // let game threads leave our detours / trampolines
    if (g_hwnd && g_origWndProc) SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_origWndProc);
    Sleep(50);
    if (g_imguiReady) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_imguiReady = false;
    }
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
    trace::Shutdown();
    for (int i = 0; i < 300 && movelist::Scanning(); ++i) Sleep(50);   // never unload under the scan thread
    MH_Uninitialize();
    Log("hot reload: stopped");
    LogClose();
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
