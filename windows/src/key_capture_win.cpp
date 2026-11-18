#include "key_capture_win.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstring>

namespace {

// Scan code set 1 -> HID usage, keys WITHOUT the extended flag.
// 104/105-key layout: the position decides, AZERTY and QWERTY share this table.
const uint8_t kScanToHid[0x59] = {
    /*00*/ 0x00, 0x29, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23,   // Esc 1 2 3 4 5 6
    /*08*/ 0x24, 0x25, 0x26, 0x27, 0x2D, 0x2E, 0x2A, 0x2B,   // 7 8 9 0 - = Backspace Tab
    /*10*/ 0x14, 0x1A, 0x08, 0x15, 0x17, 0x1C, 0x18, 0x0C,   // Q W E R T Y U I
    /*18*/ 0x12, 0x13, 0x2F, 0x30, 0x28, 0xE0, 0x04, 0x16,   // O P [ ] Enter LCtrl A S
    /*20*/ 0x07, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x33,   // D F G H J K L ;
    /*28*/ 0x34, 0x35, 0xE1, 0x31, 0x1D, 0x1B, 0x06, 0x19,   // ' ` LShift \ Z X C V
    /*30*/ 0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0xE5, 0x55,   // B N M , . / RShift Num*
    /*38*/ 0xE2, 0x2C, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E,   // LAlt Space Caps F1..F5
    /*40*/ 0x3F, 0x40, 0x41, 0x42, 0x43, 0x48, 0x47, 0x5F,   // F6..F10 Pause ScrollLock Num7
    /*48*/ 0x60, 0x61, 0x56, 0x5C, 0x5D, 0x5E, 0x57, 0x59,   // Num8 Num9 Num- Num4 Num5 Num6 Num+ Num1
    /*50*/ 0x5A, 0x5B, 0x62, 0x63, 0x00, 0x00, 0x64, 0x44,   // Num2 Num3 Num0 Num. - - ISO<> F11
    /*58*/ 0x45                                               // F12
};

struct ExtKey { uint8_t scan; uint8_t hid; };
const ExtKey kExtToHid[] = {
    { 0x1C, 0x58 },  // keypad Enter
    { 0x1D, 0xE4 },  // right Ctrl
    { 0x35, 0x54 },  // keypad /
    { 0x37, 0x46 },  // Print Screen
    { 0x38, 0xE6 },  // right Alt (AltGr)
    { 0x45, 0x53 },  // Num Lock
    { 0x47, 0x4A },  // Home
    { 0x48, 0x52 },  // Up
    { 0x49, 0x4B },  // Page Up
    { 0x4B, 0x50 },  // Left
    { 0x4D, 0x4F },  // Right
    { 0x4F, 0x4D },  // End
    { 0x50, 0x51 },  // Down
    { 0x51, 0x4E },  // Page Down
    { 0x52, 0x49 },  // Insert
    { 0x53, 0x4C },  // Delete
    { 0x5B, 0xE3 },  // left Windows
    { 0x5C, 0xE7 },  // right Windows
    { 0x5D, 0x65 },  // Menu
};

// Keys held, oldest first. Small: nobody holds more than a handful.
uint8_t g_held[16];
int     g_heldCount = 0;

void HeldAdd(uint8_t usage) {
    for (int i = 0; i < g_heldCount; i++)
        if (g_held[i] == usage) return;
    if (g_heldCount < 16) g_held[g_heldCount++] = usage;
}

void HeldRemove(uint8_t usage) {
    for (int i = 0; i < g_heldCount; i++) {
        if (g_held[i] != usage) continue;
        for (int k = i; k + 1 < g_heldCount; k++) g_held[k] = g_held[k + 1];
        g_heldCount--;
        return;
    }
}

// Reverse lookup for the labels: usage -> scan code (+ extended).
bool ScanFromHid(uint8_t usage, unsigned& scan, bool& extended) {
    for (unsigned s = 1; s < sizeof(kScanToHid); s++) {
        if (kScanToHid[s] == usage && s != 0x45) { scan = s; extended = false; return true; }
    }
    for (size_t i = 0; i < sizeof(kExtToHid) / sizeof(kExtToHid[0]); i++) {
        if (kExtToHid[i].hid == usage) { scan = kExtToHid[i].scan; extended = true; return true; }
    }
    return false;
}

const char* FixedLabel(uint8_t usage) {
    switch (usage) {
        case 0x28: return "ENTER";    case 0x29: return "ESC";      case 0x2A: return "BACKSP";
        case 0x2B: return "TAB";      case 0x2C: return "SPACE";    case 0x39: return "CAPS";
        case 0x46: return "PRT SC";   case 0x47: return "SCROLL";   case 0x48: return "PAUSE";
        case 0x49: return "INS";      case 0x4A: return "HOME";     case 0x4B: return "PG UP";
        case 0x4C: return "DEL";      case 0x4D: return "END";      case 0x4E: return "PG DN";
        case 0x4F: return "RIGHT";    case 0x50: return "LEFT";     case 0x51: return "DOWN";
        case 0x52: return "UP";       case 0x53: return "NUM LOCK"; case 0x54: return "NUM /";
        case 0x55: return "NUM *";    case 0x56: return "NUM -";    case 0x57: return "NUM +";
        case 0x58: return "NUM ENT";  case 0x62: return "NUM 0";    case 0x63: return "NUM .";
        case 0x65: return "MENU";
        case 0xE0: return "L CTRL";   case 0xE1: return "L SHIFT";  case 0xE2: return "L ALT";
        case 0xE3: return "L WIN";    case 0xE4: return "R CTRL";   case 0xE5: return "R SHIFT";
        case 0xE6: return "R ALT";    case 0xE7: return "R WIN";
        default: return nullptr;
    }
}

}  // namespace

uint8_t HidUsageFromScan(unsigned scanCode, bool extended, unsigned virtualKey) {
    // Pause and Num Lock share scan code 0x45; Windows tells them apart with
    // the virtual key (and flags Num Lock, not Pause, as extended).
    if (virtualKey == VK_PAUSE) return 0x48;
    if (virtualKey == VK_NUMLOCK) return 0x53;
    if (virtualKey == VK_SNAPSHOT) return 0x46;
    scanCode &= 0xFF;
    if (extended) {
        for (size_t i = 0; i < sizeof(kExtToHid) / sizeof(kExtToHid[0]); i++)
            if (kExtToHid[i].scan == scanCode) return kExtToHid[i].hid;
        return 0;
    }
    if (scanCode >= sizeof(kScanToHid)) return 0;
    uint8_t usage = kScanToHid[scanCode];
    // Scan code 0x2B is the key left of Enter. A US keyboard reports it as
    // usage 0x31 (backslash); an ISO keyboard (French, German, UK...) reports
    // the key at that scan code as usage 0x32.
    if (scanCode == 0x2B && LOWORD((UINT_PTR)GetKeyboardLayout(0)) != 0x0409) usage = 0x32;
    return usage;
}

bool KeyCaptureOnMessage(void* hwnd, unsigned msg, uintptr_t wParam, intptr_t lParam) {
    const bool down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
    const bool up   = (msg == WM_KEYUP || msg == WM_SYSKEYUP);
    if (!down && !up) return false;

    const unsigned scan = (unsigned)((lParam >> 16) & 0xFF);
    const bool extended = ((lParam >> 24) & 1) != 0;

    // AltGr: Windows sends a fake left Ctrl just before the right Alt, with the
    // same time stamp. Without this the capture would bind "L CTRL".
    if (down && wParam == VK_CONTROL && !extended) {
        MSG next;
        if (PeekMessageW(&next, (HWND)hwnd, WM_KEYFIRST, WM_KEYLAST, PM_NOREMOVE) &&
            (next.message == WM_KEYDOWN || next.message == WM_SYSKEYDOWN) &&
            next.wParam == VK_MENU && ((next.lParam >> 24) & 1) && next.time == (DWORD)GetMessageTime())
            return true;
    }

    const uint8_t usage = HidUsageFromScan(scan, extended, (unsigned)wParam);
    if (!usage) return true;
    if (down) HeldAdd(usage);
    else {
        HeldRemove(usage);
        // Releasing right Alt also releases the fake Ctrl that came with it.
        if (usage == 0xE6 && !(GetKeyState(VK_LCONTROL) & 0x8000)) HeldRemove(0xE0);
        // Both Shift keys held: Windows reports only one release.
        if ((usage == 0xE1 || usage == 0xE5) && !(GetKeyState(VK_SHIFT) & 0x8000)) { HeldRemove(0xE1); HeldRemove(0xE5); }
    }
    return true;
}

void KeyCaptureReset() { g_heldCount = 0; }

int32_t KeyCaptureHeldCode() {
    return g_heldCount > 0 ? (int32_t)g_held[g_heldCount - 1] : -1;
}

const char* HidKeyLabel(uint8_t usage, char* buf, int bufSize) {
    if (bufSize < 8) return "";
    buf[0] = '\0';
    // Digits and F keys: never translated ("&" on the French 1 key would only confuse).
    if (usage >= 0x1E && usage <= 0x26) { _snprintf(buf, bufSize - 1, "%d", (int)(usage - 0x1E) + 1); buf[bufSize - 1] = 0; return buf; }
    if (usage == 0x27) { strcpy(buf, "0"); return buf; }
    if (usage >= 0x3A && usage <= 0x45) { _snprintf(buf, bufSize - 1, "F%d", (int)(usage - 0x3A) + 1); buf[bufSize - 1] = 0; return buf; }
    if (usage >= 0x59 && usage <= 0x61) { _snprintf(buf, bufSize - 1, "NUM %d", (int)(usage - 0x59) + 1); buf[bufSize - 1] = 0; return buf; }
    if (const char* fixed = FixedLabel(usage)) { _snprintf(buf, bufSize - 1, "%s", fixed); buf[bufSize - 1] = 0; return buf; }

    // Letters and symbols: the character this key prints with the user's layout.
    unsigned scan = 0;
    bool extended = false;
    if (ScanFromHid(usage == 0x32 ? 0x31 : usage, scan, extended) && !extended) {
        const HKL layout = GetKeyboardLayout(0);
        const UINT vk = MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK, layout);
        const UINT ch = vk ? (MapVirtualKeyExW(vk, MAPVK_VK_TO_CHAR, layout) & 0x7FFF) : 0;
        if (ch > 0x20 && ch != 0x7F) {
            wchar_t w[2] = { (wchar_t)ch, 0 };
            CharUpperBuffW(w, 1);
            const int n = WideCharToMultiByte(CP_UTF8, 0, w, 1, buf, bufSize - 1, nullptr, nullptr);
            if (n > 0) { buf[n] = '\0'; return buf; }
        }
    }
    _snprintf(buf, bufSize - 1, "0x%02X", (unsigned)usage);
    buf[bufSize - 1] = '\0';
    return buf;
}

const char* KeypadKeyLabel(uint8_t usage) {
    switch (usage) {
        case 0x1E: return "01"; case 0x1F: return "02"; case 0x20: return "03"; case 0x21: return "04";
        case 0x22: return "05"; case 0x2B: return "06"; case 0x14: return "07"; case 0x1A: return "08";
        case 0x08: return "09"; case 0x15: return "10"; case 0x39: return "11"; case 0x04: return "12";
        case 0x16: return "13"; case 0x07: return "14"; case 0x09: return "15"; case 0xE1: return "16";
        case 0x1D: return "17"; case 0x1B: return "18"; case 0x06: return "19"; case 0x2C: return "20";
        case 0xE2: return "THUMB";
        // The thumbpad sends the arrow keys: the usual arrow labels (and their
        // icons) already say it.
        default: return nullptr;
    }
}
