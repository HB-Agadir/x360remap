// X360Remap for Windows: the X360Remap Studio on the PC.
//
// Window, Direct3D 11 device, Dear ImGui (same v1.72b core as the console
// Studio, official Win32 + DX11 backends) and the frame loop. The pages are
// the console ones (application/ui_glossy.cpp), drawn on a 16:9 canvas that
// follows the window; the status bar under it is the only PC-specific strip.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>

#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "../../application/i18n.h"
#include "../../application/theme.h"

#include "key_capture_win.h"
#include "live_watch.h"
#include "model_win.h"
#include "guide_win.h"
#include "platform_win.h"
#include "sync.h"
#include "ui_win.h"
#include "camfinder_win.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

// Console resolution: every position in the shared pages is in these units.
const float kDesignW = 1280.0f, kDesignH = 720.0f, kDesignBar = 36.0f;
const float kClientAspect = (kDesignH + kDesignBar) / kDesignW;   // height / width
const int   kMinClientW = 960;

HWND                    g_hwnd = nullptr;
ID3D11Device*           g_device = nullptr;
ID3D11DeviceContext*    g_context = nullptr;
IDXGISwapChain*         g_swapChain = nullptr;
ID3D11RenderTargetView* g_target = nullptr;
bool                    g_ready = false;        // ImGui and the pages are initialised
bool                    g_inSizeMove = false;
bool                    g_inFrame = false;

std::vector<float>      g_fontBaseSizes;         // sizes at scale 1 (console sizes)
float                   g_builtScale = 1.0f;

// --screenshot=<file.bmp>: render a few seconds, save the picture, quit.
// The PC counterpart of preview/ ("PC preview before the console").
std::string             g_shotPath;
double                  g_shotAfter = 3.0;

void SaveBackBuffer(const std::string& path) {
    ID3D11Texture2D* back = nullptr;
    if (FAILED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) return;
    D3D11_TEXTURE2D_DESC desc;
    back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D* copy = nullptr;
    if (SUCCEEDED(g_device->CreateTexture2D(&desc, nullptr, &copy)) && copy) {
        g_context->CopyResource(copy, back);
        D3D11_MAPPED_SUBRESOURCE map;
        if (SUCCEEDED(g_context->Map(copy, 0, D3D11_MAP_READ, 0, &map))) {
            std::string bmp;
            BITMAPFILEHEADER fh;
            BITMAPINFOHEADER ih;
            ZeroMemory(&fh, sizeof(fh));
            ZeroMemory(&ih, sizeof(ih));
            const DWORD pixels = desc.Width * desc.Height * 4;
            fh.bfType = 0x4D42;
            fh.bfOffBits = sizeof(fh) + sizeof(ih);
            fh.bfSize = fh.bfOffBits + pixels;
            ih.biSize = sizeof(ih);
            ih.biWidth = (LONG)desc.Width;
            ih.biHeight = -(LONG)desc.Height;   // top-down
            ih.biPlanes = 1;
            ih.biBitCount = 32;
            bmp.append((const char*)&fh, sizeof(fh));
            bmp.append((const char*)&ih, sizeof(ih));
            bmp.resize(fh.bfSize);
            for (UINT y = 0; y < desc.Height; y++) {
                const unsigned char* src = (const unsigned char*)map.pData + (size_t)y * map.RowPitch;
                unsigned char* dst = (unsigned char*)&bmp[fh.bfOffBits + (size_t)y * desc.Width * 4];
                for (UINT x = 0; x < desc.Width; x++) {   // RGBA -> BGRA
                    dst[x * 4 + 0] = src[x * 4 + 2];
                    dst[x * 4 + 1] = src[x * 4 + 1];
                    dst[x * 4 + 2] = src[x * 4 + 0];
                    dst[x * 4 + 3] = 255;
                }
            }
            g_context->Unmap(copy, 0);
            plat::WriteWholeFile(path, bmp);
        }
        copy->Release();
    }
    back->Release();
}

void CreateTarget() {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back))) && back) {
        g_device->CreateRenderTargetView(back, nullptr, &g_target);
        back->Release();
    }
}

void ReleaseTarget() {
    if (g_target) { g_target->Release(); g_target = nullptr; }
}

bool CreateDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    // Hardware first; the software device (WARP) keeps the tool usable on a PC
    // or a virtual machine without a working graphics driver.
    const D3D_DRIVER_TYPE types[2] = { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP };
    for (int i = 0; i < 2; i++) {
        if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, types[i], nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                                    &sd, &g_swapChain, &g_device, &got, &g_context))) {
            CreateTarget();
            return g_target != nullptr;
        }
    }
    return false;
}

void DestroyDevice() {
    ReleaseTarget();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

// The shared pages draw text at "design size x scale" from fonts baked at a
// fixed size. Re-baking the same fonts at the scaled size keeps the text sharp
// in a large window, without touching the shared code.
void ApplyScale(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    for (int i = 0; i < io.Fonts->ConfigData.Size && i < (int)g_fontBaseSizes.size(); i++)
        io.Fonts->ConfigData[i].SizePixels = g_fontBaseSizes[i] * scale;
    io.Fonts->TexDesiredWidth = scale > 1.25f ? 4096 : 2048;
    io.Fonts->Build();
    ImGui_ImplDX11_InvalidateDeviceObjects();   // the backend re-creates the font texture

    ImGui::GetStyle() = ImGuiStyle();
    ApplyX360RemapTheme();
    ImGui::GetStyle().ScaleAllSizes(scale);
    g_builtScale = scale;
}

struct Layout {
    float clientW, clientH;   // window client area
    float canvasW, canvasH;   // 16:9 pages
    float barH;               // status bar under the pages
    float offX, offY;         // top-left of the canvas in the client area
    float gapBelow;           // between the pages and the status bar
};

Layout ComputeLayout() {
    Layout l;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    l.clientW = (float)(rc.right - rc.left);
    l.clientH = (float)(rc.bottom - rc.top);
    float w = l.clientW;
    if (w * kClientAspect > l.clientH) w = l.clientH / kClientAspect;
    l.canvasW = floorf(w);
    l.canvasH = floorf(l.canvasW * kDesignH / kDesignW);
    l.barH = floorf(UiWinBarHeight(l.canvasW));
    l.offX = floorf((l.clientW - l.canvasW) * 0.5f);
    // The status bar is pinned to the bottom of the window; spare height goes
    // above and below the pages equally.
    l.offY = floorf((l.clientH - l.canvasH - l.barH) * 0.5f);
    if (l.offX < 0.0f) l.offX = 0.0f;
    if (l.offY < 0.0f) l.offY = 0.0f;
    l.gapBelow = l.clientH - l.canvasH - l.barH - l.offY;
    if (l.gapBelow < 0.0f) l.gapBelow = 0.0f;
    return l;
}

void Frame() {
    if (!g_ready || g_inFrame || IsIconic(g_hwnd)) return;
    const Layout l = ComputeLayout();
    if (l.canvasW < 64.0f) return;
    g_inFrame = true;

    const float scale = l.canvasW / kDesignW;
    if (!g_inSizeMove && fabsf(scale - g_builtScale) > 0.02f) ApplyScale(scale);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    // ImGui works in canvas coordinates: (0,0) is the top-left of the pages.
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(l.canvasW, l.canvasH + l.gapBelow + l.barH);
    if (io.MousePos.x > -FLT_MAX * 0.5f) { io.MousePos.x -= l.offX; io.MousePos.y -= l.offY; }
    ImGui::NewFrame();

    consync::Update();

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(l.canvasW, l.canvasH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);
    ModelDrawPages(l.canvasW, l.canvasH);
    ImGui::End();

    UiWinDraw(l.canvasW, l.canvasH + l.gapBelow, g_hwnd);

    // Around the 16:9 pages (window not exactly 16:9, maximised, full screen):
    // the page background continues to the edges of the window and the status
    // bar spans its whole width. No black band.
    static ImDrawList* s_surround = new ImDrawList(ImGui::GetDrawListSharedData());
    const float x0 = -l.offX, y0 = -l.offY, x1 = l.clientW - l.offX, y1 = l.clientH - l.offY;
    const float barTop = l.canvasH + l.gapBelow;
    const bool surround = l.offX > 0.0f || l.offY > 0.0f || l.gapBelow > 0.0f;
    if (surround) {
        s_surround->Clear();
        s_surround->PushClipRect(ImVec2(x0, y0), ImVec2(x1, y1));
        s_surround->PushTextureID(io.Fonts->TexID);
        if (ImTextureID bg = (ImTextureID)ModelBackgroundTexture()) {
            const float e = 0.002f;   // edge of the picture, stretched outwards
            if (l.offY > 0.0f) s_surround->AddImage(bg, ImVec2(x0, y0), ImVec2(x1, 0.0f), ImVec2(0, 0), ImVec2(1, e));
            if (l.gapBelow > 0.0f) s_surround->AddImage(bg, ImVec2(x0, l.canvasH), ImVec2(x1, barTop), ImVec2(0, 1 - e), ImVec2(1, 1));
            if (l.offX > 0.0f) {
                s_surround->AddImage(bg, ImVec2(x0, 0.0f), ImVec2(0.0f, l.canvasH), ImVec2(0, 0), ImVec2(e, 1));
                s_surround->AddImage(bg, ImVec2(l.canvasW, 0.0f), ImVec2(x1, l.canvasH), ImVec2(1 - e, 0), ImVec2(1, 1));
            }
        }
        s_surround->AddRectFilled(ImVec2(x0, barTop), ImVec2(x1, y1), IM_COL32(9, 14, 22, 255));
    }

    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    // Back to window coordinates: the canvas is centred in the client area.
    dd->DisplayPos = ImVec2(-l.offX, -l.offY);
    dd->DisplaySize = ImVec2(l.clientW, l.clientH);

    const float clear[4] = { 0.035f, 0.055f, 0.085f, 1.0f };
    g_context->OMSetRenderTargets(1, &g_target, nullptr);
    g_context->ClearRenderTargetView(g_target, clear);
    if (surround) {
        ImDrawList* lists[1] = { s_surround };
        ImDrawData around;
        around.Valid = true;
        around.CmdLists = lists;
        around.CmdListsCount = 1;
        around.TotalVtxCount = s_surround->VtxBuffer.Size;
        around.TotalIdxCount = s_surround->IdxBuffer.Size;
        around.DisplayPos = dd->DisplayPos;
        around.DisplaySize = dd->DisplaySize;
        around.FramebufferScale = ImVec2(1.0f, 1.0f);
        ImGui_ImplDX11_RenderDrawData(&around);
    }
    ImGui_ImplDX11_RenderDrawData(dd);
    if (!g_shotPath.empty() && plat::Seconds() > g_shotAfter) {
        SaveBackBuffer(g_shotPath);
        g_shotPath.clear();
        PostQuitMessage(0);
    }
    g_swapChain->Present(1, 0);
    g_inFrame = false;
}

// Non-client size (title bar, borders) of the window, to convert between the
// window rectangle and its client area.
void FrameExtents(HWND hwnd, int& extraW, int& extraH) {
    RECT wr, cr;
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);
    extraW = (wr.right - wr.left) - (cr.right - cr.left);
    extraH = (wr.bottom - wr.top) - (cr.bottom - cr.top);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    KeyCaptureOnMessage(hwnd, msg, wParam, lParam);
    if (g_ready && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return 1;

    switch (msg) {
    case WM_SIZE:
        if (g_device && wParam != SIZE_MINIMIZED) {
            ReleaseTarget();
            g_swapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateTarget();
            Frame();   // keeps the window painted while its border is dragged
        }
        return 0;
    case WM_ENTERSIZEMOVE:
        g_inSizeMove = true;
        return 0;
    case WM_EXITSIZEMOVE:
        g_inSizeMove = false;
        return 0;
    case WM_KEYDOWN:
        // F11: full screen and back (not while a key is being captured, and
        // not on auto-repeat).
        if (wParam == VK_F11 && !(lParam & 0x40000000) && g_ready && !ModelCapturing()) {
            static WINDOWPLACEMENT s_before = { sizeof(WINDOWPLACEMENT) };
            const DWORD style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
            if (style & WS_OVERLAPPEDWINDOW) {
                MONITORINFO mi = { sizeof(mi) };
                if (GetWindowPlacement(hwnd, &s_before) && GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
                    SetWindowLongPtrW(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
                    SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                                 mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
                }
            } else {
                SetWindowLongPtrW(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
                SetWindowPlacement(hwnd, &s_before);
                SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            }
            return 0;
        }
        break;
    case WM_GETMINMAXINFO: {
        int extraW, extraH;
        FrameExtents(hwnd, extraW, extraH);
        MINMAXINFO* mm = (MINMAXINFO*)lParam;
        mm->ptMinTrackSize.x = kMinClientW + extraW;
        mm->ptMinTrackSize.y = (int)(kMinClientW * kClientAspect + 0.5f) + extraH;
        return 0;
    }
    case WM_DEVICECHANGE:
        if (g_ready) ModelRescanPcDevices();   // a keyboard / mouse / controller plugged into the PC
        break;
    case WM_DPICHANGED: {
        const RECT* r = (const RECT*)lParam;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_KILLFOCUS:
        KeyCaptureReset();
        if (g_ready) memset(ImGui::GetIO().KeysDown, 0, sizeof(ImGui::GetIO().KeysDown));
        return 0;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        // While a key is being captured, Alt / F10 / Alt+F4 are keys like any
        // other: they must reach the capture, not the window menu.
        if (g_ready && ModelCapturing()) return 0;
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU) return 0;   // no menu: Alt alone must not freeze the window
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCmd) {
    // --data-dir=<folder>: keep settings and working copy there (tests, or
    // several consoles side by side).
    // The other options exist for screenshots and checks without a mouse:
    //   --tab=0..4  --open=connection|aim|game|camfinder[-running|-found|-failed]|logs|advanced|shortcuts
    //   --game=<Title ID hex>  --width=<client width>  --no-connect
    //   --screenshot=<file.bmp> [--after=<seconds>]   (renders, saves, quits)
    std::string dataDir, optOpen, watchDir;
    int watchSeconds = 3600;
    int optTab = -1, optWidth = 0, optHeight = 0;
    unsigned optGame = 0;
    bool optNoConnect = false;
    for (int i = 1; i < __argc; i++) {
        char arg[1024] = "";
        WideCharToMultiByte(CP_UTF8, 0, __wargv[i], -1, arg, sizeof(arg) - 1, nullptr, nullptr);
        if (strncmp(arg, "--data-dir=", 11) == 0) dataDir = arg + 11;
        else if (strncmp(arg, "--tab=", 6) == 0) optTab = atoi(arg + 6);
        else if (strncmp(arg, "--guide=", 8) == 0) GuideShowStep(atoi(arg + 8), strchr(arg, ',') ? atoi(strchr(arg, ',') + 1) : 0);   // screenshots: --guide=0,3
        else if (strncmp(arg, "--open=", 7) == 0) optOpen = arg + 7;
        else if (strncmp(arg, "--game=", 7) == 0) optGame = (unsigned)strtoul(arg + 7, nullptr, 16);
        else if (strncmp(arg, "--width=", 8) == 0) optWidth = atoi(arg + 8);
        else if (strncmp(arg, "--height=", 9) == 0) optHeight = atoi(arg + 9);
        else if (strncmp(arg, "--screenshot=", 13) == 0) g_shotPath = arg + 13;
        else if (strncmp(arg, "--after=", 8) == 0) g_shotAfter = atof(arg + 8);
        else if (strcmp(arg, "--no-connect") == 0) optNoConnect = true;
        else if (strncmp(arg, "--watch-dir=", 12) == 0) watchDir = arg + 12;
        else if (strncmp(arg, "--watch-seconds=", 16) == 0) watchSeconds = atoi(arg + 16);
    }
    plat::Init(dataDir.c_str());
    plat::Log("X360Remap for Windows %s starting", kWinAppVersion);

    // --watch-dir=<folder> [--watch-seconds=N]: no window, follows the console
    // until it freezes (or N seconds), so a script can run the whole test.
    if (!watchDir.empty()) {
        const WinSettings& cfg = plat::Settings();
        livewatch::Params p;
        p.host = cfg.consoleIp; p.ftpPort = cfg.ftpPort; p.ftpUser = cfg.ftpUser;
        p.ftpPassword = cfg.ftpPassword; p.remoteDir = cfg.remoteDir; p.outDir = watchDir;
        CreateDirectoryA(watchDir.c_str(), nullptr);
        livewatch::Start(p);
        const ULONGLONG end = GetTickCount64() + (ULONGLONG)watchSeconds * 1000;
        while (GetTickCount64() < end) Sleep(500);   // a busy boot can look like a freeze: keep watching
        livewatch::Stop();
        return 0;
    }

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wc.lpszClassName = L"X360RemapWindows";
    RegisterClassExW(&wc);

    // First size: the console resolution at the screen's scaling, reduced if
    // the screen is too small for it.
    const DWORD style = WS_OVERLAPPEDWINDOW;
    const UINT dpi = GetDpiForSystem();
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT probe = { 0, 0, 100, 100 };
    AdjustWindowRectExForDpi(&probe, style, FALSE, 0, dpi);
    const int extraW = (probe.right - probe.left) - 100, extraH = (probe.bottom - probe.top) - 100;
    int cw = optWidth > 0 ? optWidth : MulDiv((int)kDesignW, (int)dpi, 96);
    const int maxW = (work.right - work.left) - extraW, maxH = (work.bottom - work.top) - extraH;
    if (cw > maxW) cw = maxW;
    if ((int)(cw * kClientAspect) > maxH) cw = (int)(maxH / kClientAspect);
    if (cw < kMinClientW) cw = kMinClientW;
    int ch = (int)(cw * kClientAspect + 0.5f);
    if (optHeight > 0) ch = optHeight < maxH ? optHeight : maxH;
    const int winW = cw + extraW, winH = ch + extraH;
    int x = work.left + ((work.right - work.left) - winW) / 2, y = work.top + ((work.bottom - work.top) - winH) / 2;
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;

    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"X360Remap Studio", style, x, y, winW, winH, nullptr, nullptr, instance, nullptr);
    if (!g_hwnd) return 1;
    if (!CreateDevice(g_hwnd)) {
        DestroyDevice();
        MessageBoxW(g_hwnd, L"Direct3D 11 could not be started on this PC.", L"X360Remap Studio", MB_ICONERROR);
        return 1;
    }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // nothing to remember: the layout is fixed
    ApplyX360RemapTheme();
    LoadX360RemapFont(24.0f);   // same call as the console Studio
    for (int i = 0; i < io.Fonts->ConfigData.Size; i++) g_fontBaseSizes.push_back(io.Fonts->ConfigData[i].SizePixels);
    plat::Log("%s", GetFontAtlasReport());
    plat::InstallImGuiClipboard();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    {
        const int bad = CheckStringTableIntegrity();
        if (bad >= 0) plat::Log("WARNING translation table shifted at index %d", bad);
    }
    SetLanguage((Lang)plat::Settings().language);
    ModelInit(g_device);
    consync::Start();
    if (plat::Settings().autoConnect && plat::Settings().consoleIp[0] && !optNoConnect) consync::Pull();
    if (plat::Settings().checkUpdates && !optNoConnect) consync::CheckForUpdate();   // off unless the user turned it on
    if (optTab >= 0) ModelSetTab(optTab);
    if (optGame) ModelSelectGame(optGame);
    if (!optOpen.empty()) UiWinOpenDialog(optOpen.c_str());
    // First start (no console known yet): the install wizard opens by itself.
    else if (!plat::Settings().consoleIp[0] && g_shotPath.empty()) UiWinOpenDialog("wizard");
    if (!g_shotPath.empty()) { ModelSkipSplash(); showCmd = SW_SHOWNOACTIVATE; }

    g_ready = true;
    ShowWindow(g_hwnd, showCmd);
    UpdateWindow(g_hwnd);

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }
        if (IsIconic(g_hwnd)) { Sleep(50); continue; }
        Frame();
    }

    g_ready = false;
    CamFinderShutdown();
    consync::Stop();
    plat::SaveSettings();
    ModelShutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyDevice();
    DestroyWindow(g_hwnd);
    UnregisterClassW(wc.lpszClassName, instance);
    return 0;
}
