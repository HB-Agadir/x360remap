// Test of the Studio overlay preview (application/overlay_preview.cpp).
// Compile (vcvars64 first), from a scratch folder:
//   cl /nologo /EHsc /I<application> test_overlay_preview.cpp ..\overlay_preview.cpp
// Run: test_overlay_preview.exe <out.raw> <keys|pad|none> <style 0|1>
// out.raw = 1280x720 RGBA bytes, for a picture check. Exit 0 = pass.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "../overlay_preview.h"

static uint32_t PackRgba(unsigned r, unsigned g, unsigned b, unsigned a) {
    return r | (g << 8) | (b << 16) | (a << 24);   // bytes in memory: R, G, B, A
}

static int CountDrawn(const std::vector<uint8_t>& buf) {
    int n = 0;
    for (size_t i = 3; i < buf.size(); i += 4) n += buf[i] != 0;
    return n;
}

// The settings file must give back what was saved (2026-11-12: the text was one "%X" short
// and the shortcut came back as the letter A).
static bool SettingsRoundTrip(const char* path) {
    OverlaySettings a, b;
    a.colorOn = 0x123456; a.colorOff = 0x654321; a.keysX = 400; a.keysY = 300; a.infoX = 500; a.infoY = 290;
    a.infoAlways = true; a.style = 0; a.showKeys = false; a.showInfo = false; a.showPad = true; a.showStats = false;
    a.infoSecs = 8; a.hotkey = 0xE0;
    if (!OverlaySave(path, a) || !OverlayLoad(path, b)) return false;
    return a.colorOn == b.colorOn && a.colorOff == b.colorOff && a.keysX == b.keysX && a.keysY == b.keysY &&
           a.infoX == b.infoX && a.infoY == b.infoY && a.infoAlways == b.infoAlways && a.style == b.style &&
           a.showKeys == b.showKeys && a.showInfo == b.showInfo && a.showPad == b.showPad &&
           a.showStats == b.showStats && a.infoSecs == b.infoSecs && a.hotkey == b.hotkey;
}

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: out.raw keys|pad|none 0|1\n"); return 2; }
    {
        char settingsPath[512];
        sprintf(settingsPath, "%s.settings.txt", argv[1]);
        if (!SettingsRoundTrip(settingsPath)) { printf("settings round trip: FAIL\n"); return 4; }
        printf("settings round trip: ok\n");
    }
    OverlaySettings s;
    s.showKeys = strcmp(argv[2], "keys") == 0;
    s.showPad = strcmp(argv[2], "pad") == 0;
    s.showStats = true;
    s.style = atoi(argv[3]);
    std::vector<uint8_t> buf(OVPV_W * OVPV_H * 4, 0xEE);   // junk, the renderer must clear it
    OverlayPreviewRender(s, buf.data(), OVPV_W * 4, PackRgba);
    const int drawn = CountDrawn(buf);
    printf("drawn pixels: %d\n", drawn);
    FILE* f = fopen(argv[1], "wb");
    if (!f) return 3;
    fwrite(buf.data(), 1, buf.size(), f);
    fclose(f);
    return drawn > 0 ? 0 : 1;   // "none" still draws the stats line
}
