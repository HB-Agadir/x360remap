#pragma once
// Learned cameras of one game in camera_addresses.txt (CONTRACT.md), for the
// Studio's game page. Plain C++ so a test can run it on the PC.
#include <cstdio>
#include <string>
#include "../X360Remap/camera_learn.h"

// 0 = no line, 1 = horizontal only, 2 = both axes.
inline int CameraFileState(const char* path, unsigned titleId) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    int state = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        LearnedCamera e;
        if (!CamParseLine(line, &e) || e.titleId != titleId) continue;
        const int s = e.pitchAddr ? 2 : 1;
        if (s > state) state = s;
    }
    fclose(f);
    return state;
}

// Removes every build of this game; the old file stays at backupPath. false = nothing changed.
inline bool CameraFileRemove(const char* path, const char* backupPath, unsigned titleId) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    std::string all, kept;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        all += line;
        LearnedCamera e;
        if (!CamParseLine(line, &e) || e.titleId != titleId) kept += line;
    }
    fclose(f);
    if (kept.size() == all.size()) return false;
    FILE* b = fopen(backupPath, "wb");
    if (!b) return false;
    const bool saved = fwrite(all.data(), 1, all.size(), b) == all.size();
    if (fclose(b) != 0 || !saved) return false;
    FILE* o = fopen(path, "wb");
    if (!o) return false;
    const bool written = fwrite(kept.data(), 1, kept.size(), o) == kept.size();
    return fclose(o) == 0 && written;
}
