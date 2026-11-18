#pragma once
// A USB controller plugged into the PC, read with Windows' joystick API (winmm). Button n
// here is HID button idx n, the same number the plugin uses on the console (CONTRACT.md
// "buttons", "padBtn"), so a press on the PC can be bound for the console directly.
#include <stdint.h>
#include <string>
#include "../../X360Remap/known_controllers.h"
#include "../../application/overlay_preview.h"

struct PcPadRaw {
    bool        present;
    uint16_t    vid, pid;
    std::string name;                       // SDL list name, or Windows' product name
    uint32_t    buttons;                    // bit n = HID button idx n held
    int         pov;                        // hat in hundredths of a degree, -1 = centred
    int         axes[6]; int axisCount;     // 0..65535, in X Y Z R U V order (present ones)
    const KnownControllerEntry* entry;      // SDL list entry, nullptr when unknown
    PcPadRaw() : present(false), vid(0), pid(0), buttons(0), pov(-1), axisCount(0), entry(nullptr) {}
};

// First USB controller found (looked up again every 2 s while none). False if none.
bool ReadPcPad(PcPadRaw& out);

// Xbox view of a known controller through the SDL list (no-op when entry is null).
void PcPadToXbox(const PcPadRaw& r, OverlayLiveInput& in);
