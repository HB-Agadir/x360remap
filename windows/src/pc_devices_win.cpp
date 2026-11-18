// Keyboards, mice and controllers plugged into this PC (declared in
// platform_win.h). Separate file: the tests link it without ImGui.
#include "platform_win.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <hidsdi.h>

#include <cctype>
#include <cstdlib>

#pragma comment(lib, "hid.lib")

namespace {
std::string Narrow(const wchar_t* w) {
    if (!w || !w[0]) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    s.resize(n > 0 ? (size_t)n - 1 : 0);
    return s;
}
}  // namespace

namespace plat {

bool ParseVidPid(const std::string& path, uint16_t& vid, uint16_t& pid) {
    std::string p = path;
    for (size_t i = 0; i < p.size(); i++) p[i] = (char)toupper((unsigned char)p[i]);
    std::string::size_type v = p.find("VID_"), q = p.find("PID_");
    int vLen = 4;
    if (v == std::string::npos) {   // Bluetooth: VID&0002046D_PID&B023 (2 digits of source, then the id)
        v = p.find("VID&");
        q = p.find("PID&");
        if (v == std::string::npos || q == std::string::npos) return false;
        v += 4 + 4;   // "VID&" + "0002"
        q += 4;
    } else {
        if (q == std::string::npos) return false;
        v += 4;
        q += 4;
    }
    if (v + vLen > p.size() || q + 4 > p.size()) return false;
    char* end = nullptr;
    const std::string vs = p.substr(v, 4), ps = p.substr(q, 4);
    const unsigned long vv = strtoul(vs.c_str(), &end, 16);
    if (*end) return false;
    const unsigned long pv = strtoul(ps.c_str(), &end, 16);
    if (*end) return false;
    vid = (uint16_t)vv;
    pid = (uint16_t)pv;
    return vid != 0;
}

std::vector<PcDevice> ListPcDevices() {
    std::vector<PcDevice> out;
    UINT n = 0;
    if (GetRawInputDeviceList(nullptr, &n, sizeof(RAWINPUTDEVICELIST)) != 0 || n == 0) return out;
    std::vector<RAWINPUTDEVICELIST> list(n);
    n = GetRawInputDeviceList(&list[0], &n, sizeof(RAWINPUTDEVICELIST));
    if (n == (UINT)-1) return out;
    for (UINT i = 0; i < n; i++) {
        RID_DEVICE_INFO info;
        info.cbSize = sizeof(info);
        UINT size = sizeof(info);
        if (GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICEINFO, &info, &size) == (UINT)-1) continue;
        char kind = 0;
        if (info.dwType == RIM_TYPEKEYBOARD) kind = 'K';
        else if (info.dwType == RIM_TYPEMOUSE) kind = 'M';
        else if (info.dwType == RIM_TYPEHID && info.hid.usUsagePage == 1 && (info.hid.usUsage == 4 || info.hid.usUsage == 5)) kind = 'G';
        if (!kind) continue;

        wchar_t path[512] = L"";
        size = 512;
        if (GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICENAME, path, &size) == (UINT)-1) continue;
        PcDevice d;
        d.kind = kind;
        if (!ParseVidPid(Narrow(path), d.vid, d.pid)) continue;
        bool dup = false;
        for (size_t k = 0; k < out.size() && !dup; k++) dup = out[k].vid == d.vid && out[k].pid == d.pid && out[k].kind == kind;
        if (dup) continue;
        // Name: the product string of the device (no access rights needed).
        HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            wchar_t name[128] = L"";
            if (HidD_GetProductString(h, name, sizeof(name))) d.name = Narrow(name);
            CloseHandle(h);
        }
        out.push_back(d);
    }
    return out;
}

}  // namespace plat
