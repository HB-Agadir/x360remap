#pragma once
// Xenia Camera Finder inside the Studio (HB, 2026-11-05): the search of
// ../../xenia-camera-finder/src, compiled in as is, run on its own thread so
// the window never freezes. A found camera goes straight into the game's
// direct aim lines (ImportCameraLines), ready for the usual Send.
#include <stdint.h>
#include <string>

enum CamJobState { CAMJOB_IDLE, CAMJOB_RUNNING, CAMJOB_FOUND, CAMJOB_FAILED, CAMJOB_CANCELLED };
enum CamJobError {
    CAMERR_NONE,
    CAMERR_NO_XENIA,        // Xenia or a game not found
    CAMERR_OTHER_GAME,      // Xenia runs another game than the selected one
    CAMERR_STUCK,           // the arrow keys did not move the picture
    CAMERR_NOT_FOUND,       // search done, nothing usable (or no saved line survived the check)
    CAMERR_SESSION_ONLY,    // found in the heap, but no pointer path: not saved
    CAMERR_REFUSED          // the line was refused by ImportCameraLines
};

struct CamJobView {
    CamJobState state = CAMJOB_IDLE;
    CamJobError error = CAMERR_NONE;
    bool check = false;          // a check (--check), not a search
    bool scanning = false;       // in the memory scan: percent is meaningful
    int percent = 0;
    std::string detail;          // the finder's own step line (English, technical)
    std::string line;            // the line now used for this game
    std::string note;            // error text, or the vertical that could not be kept
    bool verticalLost = false;
    bool replaced = false;       // check: a surviving path replaced the broken line
    uint32_t runningTitle = 0;   // CAMERR_OTHER_GAME: the game Xenia runs
};

// check = false: full search; true: re-test this game's lines and the kept
// pointer paths after a restart. savedLines: this game's lines (one per row).
// studioWindow is brought back to the front at the end.
void CamFinderStart(uint32_t titleId, bool check, const std::string& savedLines, void* studioWindow);
void CamFinderCancel();
// Call from the UI thread every frame: also imports a found line.
CamJobView CamFinderView();
void CamFinderReset();        // back to IDLE (not while running)
// --open=camfinder-running|found|failed: a fixed view, for screenshots only.
void CamFinderDemo(const char* which);
void CamFinderShutdown();     // at exit: cancels a running search and waits a little
