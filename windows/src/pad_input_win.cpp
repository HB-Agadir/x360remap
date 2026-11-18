#include "pad_input_win.h"

#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

bool ReadPcPad(PcPadRaw& out) {
    static int s_joy = -1;
    static DWORD s_nextLook = 0;
    static JOYCAPSW s_caps;
    static PcPadRaw s_id;   // what does not change while the pad stays plugged
    if (s_joy < 0) {
        out = PcPadRaw();
        if ((LONG)(GetTickCount() - s_nextLook) < 0) return false;
        s_nextLook = GetTickCount() + 2000;   // looking up is slow: every 2 s at most
        for (UINT j = 0; j < 16 && s_joy < 0; j++) {
            JOYINFOEX probe = { sizeof(probe), JOY_RETURNALL };
            if (joyGetPosEx(j, &probe) != JOYERR_NOERROR) continue;
            if (joyGetDevCapsW(j, &s_caps, sizeof(s_caps)) != JOYERR_NOERROR) continue;
            s_joy = (int)j;
            s_id = PcPadRaw();
            s_id.vid = s_caps.wMid; s_id.pid = s_caps.wPid;
            s_id.entry = FindKnownController(s_id.vid, s_id.pid);
            if (s_id.entry) s_id.name = s_id.entry->name;
            else {
                char name[64];
                WideCharToMultiByte(CP_UTF8, 0, s_caps.szPname, -1, name, sizeof(name), nullptr, nullptr);
                s_id.name = name;
            }
        }
        if (s_joy < 0) return false;
    }
    JOYINFOEX ji = { sizeof(ji), JOY_RETURNALL };
    if (joyGetPosEx((UINT)s_joy, &ji) != JOYERR_NOERROR) { s_joy = -1; out = PcPadRaw(); return false; }
    out = s_id;
    out.present = true;
    out.buttons = ji.dwButtons;
    out.pov = (ji.dwPOV != JOY_POVCENTERED && ji.dwPOV <= 36000) ? (int)ji.dwPOV : -1;
    out.axisCount = 0;
    out.axes[out.axisCount++] = (int)ji.dwXpos; out.axes[out.axisCount++] = (int)ji.dwYpos;
    if (s_caps.wCaps & JOYCAPS_HASZ) out.axes[out.axisCount++] = (int)ji.dwZpos;
    if (s_caps.wCaps & JOYCAPS_HASR) out.axes[out.axisCount++] = (int)ji.dwRpos;
    if (s_caps.wCaps & JOYCAPS_HASU) out.axes[out.axisCount++] = (int)ji.dwUpos;
    if (s_caps.wCaps & JOYCAPS_HASV) out.axes[out.axisCount++] = (int)ji.dwVpos;
    return true;
}

void PcPadToXbox(const PcPadRaw& r, OverlayLiveInput& in) {
    const KnownControllerEntry* e = r.entry;
    if (!r.present || !e) return;
    // order of KnownControllerEntry::btn: a b x y lb rb lt rt back start l3 r3 guide dl dr du dd
    static const uint16_t kBit[KC_BUTTONS] = { 0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0, 0,
                                               0x0020, 0x0010, 0x0040, 0x0080, 0, 0x0004, 0x0008, 0x0001, 0x0002 };
    for (int b = 0; b < KC_BUTTONS; b++) {
        const int n = e->btn[b];
        if (n < 0 || n > 31 || !(r.buttons & (1u << n))) continue;
        if (b == 6) in.lt = 255;
        else if (b == 7) in.rt = 255;
        else in.padButtons |= kBit[b];
    }
    if (r.pov >= 0) {   // d-pad on the hat, 0 = up
        const int a = r.pov;
        if (a >= 31500 || a <= 4500)  in.padButtons |= 0x0001;
        if (a >= 4500 && a <= 13500)  in.padButtons |= 0x0008;
        if (a >= 13500 && a <= 22500) in.padButtons |= 0x0002;
        if (a >= 22500 && a <= 31500) in.padButtons |= 0x0004;
    }
    int16_t* outs[4] = { &in.lx, &in.ly, &in.rx, &in.ry };
    for (int a = 0; a < 4; a++) {   // SDL axis n = n-th present axis
        const int n = e->axis[a];
        if (n < 0 || n >= r.axisCount) continue;
        int v = r.axes[n] - 32768;
        if ((e->invert >> a) & 1) v = -v;
        if (a == 1 || a == 3) v = -v;   // joystick Y grows downwards, XInput Y upwards
        *outs[a] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
    }
}
