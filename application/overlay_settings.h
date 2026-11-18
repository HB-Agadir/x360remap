#pragma once
// In-game overlay look: HDD:\X360RemapStudio\overlay_settings.txt (CONTRACT.md).
// One "name value" per line, values in hexadecimal. The plugin reads it at boot
// and when the Studio closes; a missing file or line = its default. Plain C++ so
// a g++ test can run it.
#include <cstdio>
#include <cstring>

struct OverlaySettings {
    unsigned colorOn, colorOff;     // RRGGBB: pressed key, resting key
    unsigned keysX, keysY, infoX, infoY;
    bool     infoAlways;            // info_always: aim panel never hides
    int      style;                 // 1 = images, 0 = drawn
    bool     showKeys, showInfo, showPad, showStats;   // show_keys, show_info, show_pad, show_stats (keys and pad never both on)
    int      infoSecs;              // info_secs: 2, 4 or 8
    unsigned hotkey;                // hotkey: HID usage, 0xE3 = Windows

    OverlaySettings() { Reset(); }
    void Reset() {
        colorOn = 0xFFE020; colorOff = 0xFFFFFF;          // plugin defaults
        keysX = 20; keysY = 590; infoX = 1000; infoY = 40;
        infoAlways = false; style = 1;
        showKeys = showPad = false; showInfo = showStats = true; infoSecs = 4; hotkey = 0xE3;   // Windows (F-keys are taken)
    }
};

// Themes: pressed colour, resting colour.
static const unsigned kOverlayThemes[5][2] = {
    { 0xFFD21F, 0xFFFFFF }, { 0x37D6FF, 0xD6F6FF }, { 0x8BD41A, 0xE8FFC4 },
    { 0xFF5A5A, 0xFFFFFF }, { 0xCFD8E6, 0x8FA0B8 },
};
// Colours the swatches cycle through.
static const unsigned kOverlayPalette[8] = {
    0xFFD21F, 0x37D6FF, 0x8BD41A, 0xFF5A5A, 0xFF9A2E, 0xC77DFF, 0xFFFFFF, 0x8FA0B8,
};
// Three positions per axis, inside the plugin's limits (keys 790x590, panel 1010x540).
static const unsigned kOverlayKeysXs[3] = { 20, 400, 790 }, kOverlayKeysYs[3] = { 20, 300, 590 };
static const unsigned kOverlayInfoXs[3] = { 20, 500, 1010 }, kOverlayInfoYs[3] = { 40, 290, 540 };

inline int OverlayThemeOf(const OverlaySettings& s) {   // -1 = custom colours
    for (int i = 0; i < 5; i++)
        if (s.colorOn == kOverlayThemes[i][0] && s.colorOff == kOverlayThemes[i][1]) return i;
    return -1;
}
inline int OverlayGridIndex(unsigned v, const unsigned* a) {   // nearest of 3
    int best = 0;
    for (int i = 1; i < 3; i++) {
        const int d = (int)v - (int)a[i], db = (int)v - (int)a[best];
        if ((d < 0 ? -d : d) < (db < 0 ? -db : db)) best = i;
    }
    return best;
}

// The plugin draws only while enable_overlay.txt exists: wanted when something is shown.
// (show_info is ignored since 2026-11-09: there is no aim panel any more.)
inline bool OverlayShowsAnything(const OverlaySettings& s) { return s.showKeys || s.showPad || s.showStats; }

// Same picture in the preview? (the shortcut and the dead aim-panel fields do not change it)
inline bool OverlaySameLook(const OverlaySettings& a, const OverlaySettings& b) {
    return a.colorOn == b.colorOn && a.colorOff == b.colorOff && a.keysX == b.keysX && a.keysY == b.keysY &&
           a.style == b.style && a.showKeys == b.showKeys && a.showPad == b.showPad && a.showStats == b.showStats;
}

// One line of the file: "name value", the value in hexadecimal.
inline void OverlayParseLine(const char* line, OverlaySettings& s) {
    char name[16]; unsigned v = 0;
    if (sscanf(line, "%15s %x", name, &v) != 2) return;
    if (!strcmp(name, "color_on")) s.colorOn = v & 0xFFFFFF;
    else if (!strcmp(name, "color_off")) s.colorOff = v & 0xFFFFFF;
    else if (!strcmp(name, "keys_x")) s.keysX = v > 790 ? 790 : v;
    else if (!strcmp(name, "keys_y")) s.keysY = v > 590 ? 590 : v;
    else if (!strcmp(name, "info_x")) s.infoX = v > 1010 ? 1010 : v;
    else if (!strcmp(name, "info_y")) s.infoY = v > 540 ? 540 : v;
    else if (!strcmp(name, "info_always")) s.infoAlways = v != 0;
    else if (!strcmp(name, "style")) s.style = v != 0;
    else if (!strcmp(name, "show_keys")) s.showKeys = v != 0;
    else if (!strcmp(name, "show_info")) s.showInfo = v != 0;
    else if (!strcmp(name, "show_pad")) s.showPad = v != 0;
    else if (!strcmp(name, "show_stats")) s.showStats = v != 0;
    else if (!strcmp(name, "info_secs")) s.infoSecs = (v == 2 || v == 8) ? (int)v : 4;
    else if (!strcmp(name, "hotkey")) s.hotkey = (v >= 0xE0 && v <= 0xE7) ? v : 0xE3;   // the plugin's rule: modifiers only, else Windows
}

inline bool OverlayLoad(const char* path, OverlaySettings& s) {
    s.Reset();
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char line[64];
    while (fgets(line, sizeof(line), f)) OverlayParseLine(line, s);
    fclose(f);
    if (s.showKeys) s.showPad = false;   // the keys win, like the plugin
    return true;
}

// Same, from the text of the file already in memory (the Windows app sends it over FTP).
inline void OverlayParseText(const char* text, OverlaySettings& s) {
    s.Reset();
    while (*text) {
        char line[64];
        int n = 0;
        while (*text && *text != '\n') { if (n < 63) line[n++] = *text; text++; }
        if (*text) text++;
        line[n] = 0;
        OverlayParseLine(line, s);
    }
    if (s.showKeys) s.showPad = false;
}

// Writes the whole file in one go; false if the disk refused.
// One "%X" per value below, in the same order (2026-11-12: "show_stats" was missing from the
// text, so info_secs took its value and hotkey took info_secs = 4, the letter A).
inline bool OverlaySave(const char* path, const OverlaySettings& s) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "color_on %06X\r\ncolor_off %06X\r\nkeys_x %X\r\nkeys_y %X\r\ninfo_x %X\r\ninfo_y %X\r\n"
               "info_always %X\r\nstyle %X\r\nshow_keys %X\r\nshow_info %X\r\nshow_pad %X\r\nshow_stats %X\r\ninfo_secs %X\r\nhotkey %X\r\n",
            s.colorOn, s.colorOff, s.keysX, s.keysY, s.infoX, s.infoY, (unsigned)s.infoAlways,
            (unsigned)s.style, (unsigned)s.showKeys, (unsigned)s.showInfo, (unsigned)s.showPad, (unsigned)s.showStats, (unsigned)s.infoSecs, s.hotkey);
    return fclose(f) == 0;
}

// HID usage -> short name for the shortcut chip.
inline const char* OverlayKeyName(unsigned usage, char* buf, size_t n) {
    (void)n;   // buf holds at least 8 bytes; the XDK compiler has no snprintf
    if (usage >= 0x3A && usage <= 0x45) sprintf(buf, "F%u", usage - 0x39);
    else if (usage >= 0x04 && usage <= 0x1D) sprintf(buf, "%c", (char)('A' + usage - 4));
    else if (usage == 0x48) strcpy(buf, "Pause");
    else if (usage == 0xE3 || usage == 0xE7) strcpy(buf, "Windows");
    else if (usage == 0x47) strcpy(buf, "Scroll");
    else sprintf(buf, "0x%02X", usage);
    return buf;
}
