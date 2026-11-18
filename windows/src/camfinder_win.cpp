#include "camfinder_win.h"

#include <cstring>
#include <mutex>
#include <thread>

// The finder's own headers (its copy of camera_learn.h, identical to the
// plugin's): this file must not include ../../X360Remap/camera_learn.h too.
#include "../../../xenia-camera-finder/src/finder.h"
#include "../../../xenia-camera-finder/src/input_screen.h"
#include "../../../xenia-camera-finder/src/overlay.h"
#include "../../../xenia-camera-finder/src/session.h"

#include "game_tools_win.h"
#include "platform_win.h"

namespace {

std::mutex g_lock;
CamJobView g_view;
std::string g_pending;             // found line, imported on the UI thread
Finder* g_finder = nullptr;        // the running search, for Cancel

std::string FormatLine(const LearnedCamera& e) {
    char line[200] = { 0 };
    CamFormatLine(e, line, sizeof(line));
    return line;
}

void OnStatus(int lineNo, const char* text) {
    std::lock_guard<std::mutex> l(g_lock);
    if (lineNo == 1) g_view.detail = text;
    if (g_finder) {
        g_view.percent = g_finder->scanPercent;
        g_view.scanning = lineNo == 2;
    }
}

void Finish(CamJobState state, CamJobError error, const std::string& note) {
    std::lock_guard<std::mutex> l(g_lock);
    g_view.state = state;
    g_view.error = error;
    if (!note.empty()) g_view.note = note;
    g_view.scanning = false;
}

void Job(uint32_t titleId, bool check, std::string savedLines, void* studio) {
    static bool logOpen = false;
    if (!logOpen) { LogOpen((plat::DataDir() + "camera_finder_log.txt").c_str()); logOpen = true; }
    Log("--- X360Remap Studio, camera finder: %s for %08X", check ? "check" : "search", (unsigned)titleId);
    const std::string pathsPath = plat::DataDir() + "camera_paths.txt";

    XeniaProcess x;
    Finder f(x);
    f.onStatus = OnStatus;
    GameInfo g;
    std::string err;
    if (!OpenGame(x, f, &g, &err)) { Log("ERROR: %s", err.c_str()); Finish(CAMJOB_FAILED, CAMERR_NO_XENIA, err); return; }
    Log("game: \"%s\" title %08X, build %08X", g.name.c_str(), g.titleId, g.stamp);
    if (g.titleId != titleId) {
        { std::lock_guard<std::mutex> l(g_lock); g_view.runningTitle = g.titleId; }
        Finish(CAMJOB_FAILED, CAMERR_OTHER_GAME, g.name);
        return;
    }
    { std::lock_guard<std::mutex> l(g_lock); g_finder = &f; }
    f.BuildRanges(g);
    OverlayStart();
    OverlayClear();
    OverlaySet(0, "X360REMAP STUDIO - CAMERA FINDER   " + g.name);

    try {
        if (check) {
            std::vector<LearnedCamera> saved;
            size_t start = 0;
            while (start < savedLines.size()) {
                size_t end = savedLines.find('\n', start);
                if (end == std::string::npos) end = savedLines.size();
                LearnedCamera e;
                if (CamParseLine(savedLines.substr(start, end - start).c_str(), &e)) saved.push_back(e);
                start = end + 1;
            }
            const CheckResult r = CheckGame(f, g, saved, pathsPath);
            std::lock_guard<std::mutex> l(g_lock);
            if (r.replaced) { g_pending = FormatLine(r.alive[0]); g_view.replaced = true; g_view.state = CAMJOB_FOUND; }
            else if (!r.good.empty()) { g_view.line = FormatLine(r.good[0]); g_view.state = CAMJOB_FOUND; }
            else { g_view.state = CAMJOB_FAILED; g_view.error = CAMERR_NOT_FOUND; }
        } else {
            const double moved = f.CheckCameraMoves();
            if (moved >= 0 && moved < 3.0) {
                Log("WARNING: the picture barely changed when the arrow keys were pressed");
                Finish(CAMJOB_FAILED, CAMERR_STUCK, "");
            } else {
                LearnedCamera e;
                memset(&e, 0, sizeof(e));
                const bool ok = f.Run(g, &e);
                std::lock_guard<std::mutex> l(g_lock);
                g_view.verticalLost = !f.lostVertical.empty();
                if (g_view.verticalLost) { g_view.note = f.lostVertical; Log("WARNING: %s", f.lostVertical.c_str()); }
                if (ok) {
                    SaveLines(pathsPath, e.titleId, e.timeStamp, f.paths, kPathsHeader, nullptr);
                    g_pending = FormatLine(e);
                    Log("RESULT: %s", g_pending.c_str());
                    g_view.state = CAMJOB_FOUND;
                } else {
                    g_view.state = CAMJOB_FAILED;
                    g_view.error = e.yawAddr ? CAMERR_SESSION_ONLY : CAMERR_NOT_FOUND;
                    if (e.yawAddr) g_view.line = FormatLine(e);
                }
            }
        }
    } catch (const Finder::Cancelled&) {
        Finish(CAMJOB_CANCELLED, CAMERR_NONE, "");
    }
    AllKeysUp();
    OverlayStop();
    { std::lock_guard<std::mutex> l(g_lock); g_finder = nullptr; g_view.scanning = false; }
    if (studio) FocusWindow((HWND)studio);
}

}  // namespace

void CamFinderStart(uint32_t titleId, bool check, const std::string& savedLines, void* studioWindow) {
    {
        std::lock_guard<std::mutex> l(g_lock);
        if (g_view.state == CAMJOB_RUNNING) return;
        g_view = CamJobView();
        g_view.state = CAMJOB_RUNNING;
        g_view.check = check;
        g_pending.clear();
    }
    std::thread(Job, titleId, check, savedLines, studioWindow).detach();
}

void CamFinderCancel() {
    std::lock_guard<std::mutex> l(g_lock);
    if (g_finder) g_finder->cancel = true;
}

CamJobView CamFinderView() {
    std::string pending;
    {
        std::lock_guard<std::mutex> l(g_lock);
        pending.swap(g_pending);
    }
    if (!pending.empty()) {
        // Same checks as a pasted line; the model is only touched on this thread.
        std::string err;
        const bool imported = ImportCameraLines(pending, 0, err) > 0;
        std::lock_guard<std::mutex> l(g_lock);
        if (imported) g_view.line = pending;
        else { g_view.state = CAMJOB_FAILED; g_view.error = CAMERR_REFUSED; g_view.note = err.empty() ? pending : err; }
    }
    std::lock_guard<std::mutex> l(g_lock);
    return g_view;
}

void CamFinderShutdown() {
    CamFinderCancel();
    for (int i = 0; i < 50; i++) {
        { std::lock_guard<std::mutex> l(g_lock); if (g_view.state != CAMJOB_RUNNING) return; }
        Sleep(100);
    }
    AllKeysUp();                   // the search did not stop in time: never leave an arrow key down
}

void CamFinderDemo(const char* which) {
    std::lock_guard<std::mutex> l(g_lock);
    g_view = CamJobView();
    if (strcmp(which, "running") == 0) {
        g_view.state = CAMJOB_RUNNING; g_view.scanning = true; g_view.percent = 42;
        g_view.detail = "Step: horizontal scan - hands off";
    } else if (strcmp(which, "found") == 0) {
        g_view.state = CAMJOB_FOUND;
        g_view.line = "v2 425307D3 4AD9EB64 00000000 1 82A01234 00000000 000008A8 FFFFEFE4 1 -1 rad Rogue Warrior";
    } else if (strcmp(which, "failed") == 0) {
        g_view.state = CAMJOB_FAILED; g_view.error = CAMERR_STUCK;
    }
}

void CamFinderReset() {
    std::lock_guard<std::mutex> l(g_lock);
    if (g_view.state != CAMJOB_RUNNING) g_view = CamJobView();
}
