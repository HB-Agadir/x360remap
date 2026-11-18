#pragma once
// Per-game tools of the Game profiles page that only exist on the PC:
// direct-aim camera lines (camera_addresses.txt of the working copy) and
// export / import of one game's profile as a small .json file.
#include <stdint.h>
#include <string>
#include <vector>
#include "model_win.h"

struct GameCameraInfo {
    std::vector<std::string> learned;   // lines of camera_addresses.txt for this game
    std::vector<std::string> builtin;   // lines built into the plugin (camera_known.cpp)
};
void GameCameras(uint32_t titleId, GameCameraInfo& out);

// Adds the camera lines found in text (pasted from Xenia Camera Finder, one or
// more lines, comments ignored). Every line is checked with CamParseLine; if
// one is invalid nothing is written and err names it. onlyTitleId != 0 refuses
// lines of another game. A line for the same game and build replaces the old
// one. Returns the number of lines written.
int ImportCameraLines(const std::string& text, uint32_t onlyTitleId, std::string& err);

// Imports this game's lines that Xenia Camera Finder saved in
// %LOCALAPPDATA%\X360Remap\found_cameras.txt (same rules as ImportCameraLines).
// Returns how many, 0 if it has none for this game.
int ImportFoundCameras(uint32_t titleId, std::string& err);

// Removes this game's lines from camera_addresses.txt. Returns how many.
int RemoveGameCameras(uint32_t titleId);

// One game's mouse profile (+ its learned camera lines) for sharing.
bool ExportGameProfile(const ModelDevice& mouse, uint32_t titleId, const std::string& gameName, const std::string& path);
bool ImportGameProfile(const ModelDevice& mouse, const std::string& path, uint32_t& titleIdOut, std::string& err);
