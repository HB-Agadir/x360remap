// Runtime side of the controllers known in advance - see known_controllers.h.
// The table itself is in known_controllers_db.cpp (generated).
#include "known_controllers.h"
#include <string.h>
#ifdef _XBOX
#include <xtl.h>   // MemoryBarrier
#define KC_BARRIER() MemoryBarrier()
#else
#define KC_BARRIER() ((void)0)
#endif

const KnownControllerEntry* FindKnownController(uint16_t vid, uint16_t pid) {
    const uint32_t key = ((uint32_t)vid << 16) | pid;
    unsigned lo = 0, hi = kKnownControllerCount;
    while (lo < hi) {
        const unsigned mid = (lo + hi) / 2;
        const KnownControllerEntry& e = kKnownControllers[mid];
        const uint32_t k = ((uint32_t)e.vid << 16) | e.pid;
        if (k == key) return &e;
        if (k < key) lo = mid + 1; else hi = mid;
    }
    return nullptr;
}

// Output field of each button slot (same order as KC_BUTTONS).
static uint8_t ButtonsReport::* const kKcButtonField[KC_BUTTONS] = {
    &ButtonsReport::a_button, &ButtonsReport::b_button,
    &ButtonsReport::x_button, &ButtonsReport::y_button,
    &ButtonsReport::l1, &ButtonsReport::r1,
    &ButtonsReport::l2, &ButtonsReport::r2,
    &ButtonsReport::back, &ButtonsReport::start,
    &ButtonsReport::l3, &ButtonsReport::r3,
    &ButtonsReport::xbox,
    &ButtonsReport::dpad_left, &ButtonsReport::dpad_right,
    &ButtonsReport::dpad_up, &ButtonsReport::dpad_down,
};

// Output field of each axis role. The plugin's final conversion is
// LX = x, LY = y, RX = z, RY = rz, LT = rx, RT = ry (see XInputdReadStateHook).
static int16_t ButtonsReport::* const kKcAxisField[KC_AXES] = {
    &ButtonsReport::x, &ButtonsReport::y,
    &ButtonsReport::z, &ButtonsReport::rz,
    &ButtonsReport::rx, &ButtonsReport::ry,
};

struct KcPoolSlot {
    HidDeviceMapping  map;
    HidAxisMapEntry   axes[KC_AXES];
    HidButtonMapEntry buttons[KC_BUTTONS];
};
static KcPoolSlot g_kcPool[KC_POOL_SIZE];
static volatile unsigned g_kcPoolCount = 0;

HidDeviceMapping* FindKnownControllerMapping(uint16_t vid, uint16_t pid) {
    const unsigned n = g_kcPoolCount;
    KC_BARRIER();
    for (unsigned i = 0; i < n && i < KC_POOL_SIZE; i++) {
        if (g_kcPool[i].map.vendorId == vid && g_kcPool[i].map.productId == pid)
            return &g_kcPool[i].map;
    }
    return nullptr;
}

void ResetKnownControllerPool() {
    g_kcPoolCount = 0;
    for (unsigned i = 0; i < KC_POOL_SIZE; i++) g_kcPool[i] = KcPoolSlot();
}

static void SetInvert(HidAxisInvertFlags* f, uint16_t usage, bool on) {
    switch (usage) {
    case HID_USAGE_AXIS_X:  f->invertX  = on; break;
    case HID_USAGE_AXIS_Y:  f->invertY  = on; break;
    case HID_USAGE_AXIS_Z:  f->invertZ  = on; break;
    case HID_USAGE_AXIS_RX: f->invertRX = on; break;
    case HID_USAGE_AXIS_RY: f->invertRY = on; break;
    case HID_USAGE_AXIS_RZ: f->invertRZ = on; break;
    default: break;   // sliders: no inversion flag in HidAxisInvertFlags
    }
}

HidDeviceMapping* AttachKnownController(uint16_t vid, uint16_t pid,
                                        const uint16_t* descAxisUsages, uint8_t count,
                                        const char** outName) {
    if (outName) *outName = nullptr;
    const KnownControllerEntry* e = FindKnownController(vid, pid);
    if (!e) return nullptr;
    if (outName) *outName = e->name;

    HidDeviceMapping* existing = FindKnownControllerMapping(vid, pid);
    if (existing) return existing;
    if (g_kcPoolCount >= KC_POOL_SIZE) return nullptr;

    // SDL axis number -> HID usage, the DirectInput way.
    uint16_t order[8];
    uint8_t n = 0;
    for (uint16_t u = HID_USAGE_AXIS_X; u <= HID_USAGE_AXIS_RZ; u++) {
        for (uint8_t i = 0; i < count; i++) {
            if (descAxisUsages && descAxisUsages[i] == u) { order[n++] = u; break; }
        }
    }
    uint8_t sliders = 0;
    for (uint8_t i = 0; i < count && sliders < 2 && n < 8; i++) {
        const uint16_t u = descAxisUsages[i];
        if (u < 0x36 || u > 0x38) continue;           // Slider, Dial, Wheel
        bool dup = false;
        for (uint8_t k = 0; k < n; k++) if (order[k] == u) dup = true;
        if (dup) continue;
        order[n++] = u;
        sliders++;
    }

    KcPoolSlot& s = g_kcPool[g_kcPoolCount];
    s = KcPoolSlot();   // value-init: null member pointers, zero numbers
    s.map.vendorId = vid;
    s.map.productId = pid;

    uint8_t na = 0;
    bool usedUsage[8] = { false };
    for (int r = 0; r < KC_AXES; r++) {
        const int a = e->axis[r];
        if (a >= 0 && a < n) usedUsage[a] = true;
    }
    for (int r = 0; r < KC_AXES; r++) {
        int a = e->axis[r];
        // The list names an axis this controller does not have (several pads
        // share one VID:PID with different axis sets, e.g. 0079:0006). For
        // the right stick, fall back to the usual DirectInput pad layout:
        // right X = Z (else Rx), right Y = Rz (else Ry).
        if (a >= n && (r == 2 || r == 3)) {
            const uint16_t prefer[2] = { (uint16_t)(r == 2 ? HID_USAGE_AXIS_Z : HID_USAGE_AXIS_RZ),
                                         (uint16_t)(r == 2 ? HID_USAGE_AXIS_RX : HID_USAGE_AXIS_RY) };
            a = -1;
            for (int t = 0; t < 2 && a < 0; t++)
                for (uint8_t k = 0; k < n; k++)
                    if (order[k] == prefer[t] && !usedUsage[k]) { a = k; usedUsage[k] = true; break; }
        }
        if (a < 0 || a >= n) continue;
        s.axes[na].usage = order[a];
        s.axes[na].field = kKcAxisField[r];
        na++;
        // HID and SDL: down = positive. Xbox: up = positive -> the two Y
        // axes are inverted, unless SDL says the axis is already inverted.
        bool inv = (r == 1 || r == 3);
        if (e->invert & (1 << r)) inv = !inv;
        SetInvert(&s.map.invert, order[a], inv);
    }

    uint8_t nb = 0;
    for (int b = 0; b < KC_BUTTONS; b++) {
        if (e->btn[b] < 0) continue;
        s.buttons[nb].idx = (uint8_t)e->btn[b];
        s.buttons[nb].field = kKcButtonField[b];
        nb++;
    }

    s.map.axisMap = s.axes;
    s.map.axisMapCount = na;
    s.map.buttonMap = s.buttons;
    s.map.buttonMapCount = nb;

    KC_BARRIER();          // the slot is complete before it becomes visible
    g_kcPoolCount = g_kcPoolCount + 1;
    return &s.map;
}
