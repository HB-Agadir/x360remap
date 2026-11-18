#pragma once
// Controllers known in advance (2026-10-14), from the SDL Game Controller
// Database (zlib license, see tools/gamecontrollerdb_windows.txt). A
// controller in this list works the moment it is plugged in: no controller
// assistant. Its own entry in X360Remap.json (made by the assistant or the
// Studio) always wins over this list, and so do the built-in PlayStation /
// Switch mappings of mapping.cpp.
//
// The table is generated: tools/gen_known_controllers.py writes
// known_controllers_db.cpp.
#include <stdint.h>
#include "mapping.h"

#define KC_BUTTONS 17   // a b x y lb rb lt rt back start l3 r3 guide dl dr du dd
#define KC_AXES     6   // left x, left y, right x, right y, left trigger, right trigger

struct KnownControllerEntry {
    uint16_t vid;
    uint16_t pid;
    int8_t   btn[KC_BUTTONS];   // SDL button number (HID button usage - 1), -1 = none
    int8_t   axis[KC_AXES];     // SDL axis number (see AttachKnownController), -1 = none
    uint8_t  invert;            // bit i = axis[i] is inverted ("a1~" in SDL)
    const char* name;
};

extern const KnownControllerEntry kKnownControllers[];
extern const unsigned kKnownControllerCount;

// Table lookup (sorted by vid, pid). nullptr if unknown.
const KnownControllerEntry* FindKnownController(uint16_t vid, uint16_t pid);

// Builds the mapping of a known controller once its report descriptor is
// parsed. `descAxisUsages` = every Generic Desktop usage of the descriptor's
// INPUT items, in descriptor order (duplicates allowed). SDL numbers the axes
// the DirectInput way: X Y Z Rx Ry Rz that are present, in that order, then up
// to two sliders (Slider / Dial / Wheel). No allocation (fixed pool of
// KC_POOL_SIZE models), callable from the USB init callback. nullptr if the
// controller is not in the list or the pool is full. *outName = its name.
#define KC_POOL_SIZE 8
HidDeviceMapping* AttachKnownController(uint16_t vid, uint16_t pid,
                                        const uint16_t* descAxisUsages, uint8_t count,
                                        const char** outName);

// Mapping already built by AttachKnownController for this VID:PID, or
// nullptr. Used by FindMapping after the JSON and built-in mappings, so a
// hot reload of X360Remap.json keeps the controller working.
HidDeviceMapping* FindKnownControllerMapping(uint16_t vid, uint16_t pid);

// Test helper: forget every built mapping.
void ResetKnownControllerPool();
