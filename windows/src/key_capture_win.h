#pragma once
// Windows key events -> USB HID usage codes (Keyboard page 0x07), the codes
// X360Remap.json stores and the plugin reads from the console's keyboard.
//
// The PHYSICAL key is what counts (scan code), not the character: the key
// right of Tab is usage 0x14 on every keyboard, whether it prints Q (QWERTY)
// or A (AZERTY). Pressing it on the PC therefore binds the same physical key
// on the console's keyboard.
#include <stdint.h>

// Feed every WM_KEYDOWN / WM_SYSKEYDOWN / WM_KEYUP / WM_SYSKEYUP of the window
// (msg, wParam, lParam as received). Returns true if the event was a key.
bool KeyCaptureOnMessage(void* hwnd, unsigned msg, uintptr_t wParam, intptr_t lParam);
// Window lost the focus: nothing is held any more.
void KeyCaptureReset();

// HID usage of the key held now (the last one pressed), -1 if none. Same
// contract as InputStateSnapshot::keyCode on the console.
int32_t KeyCaptureHeldCode();

// Scan code (set 1) + extended flag + virtual key -> HID usage, 0 if unknown.
uint8_t HidUsageFromScan(unsigned scanCode, bool extended, unsigned virtualKey);

// Label of a key as shown in the grid: the character of the user's layout for
// letters and symbols, the usual abbreviations otherwise (ESC, L CTRL, NUM 5,
// ...). The abbreviations are the ones the console Studio uses, so the same
// key icons show up. Returns buf.
const char* HidKeyLabel(uint8_t usage, char* buf, int bufSize);

// Gaming keypads (Razer Tartarus...): number printed on the key ("01".."20",
// "THUMB") for the factory layout, or null if the usage is not a numbered key
// of the keypad (the thumbpad sends the arrow keys and keeps their labels).
// Same table as the one documented in mapping.h (OpenTartarus).
const char* KeypadKeyLabel(uint8_t usage);
