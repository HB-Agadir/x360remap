#include "game_tools_win.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include "../../application/config_writer.h"
#include "../../X360Remap/camera_learn.h"
#include "../../X360Remap/mapping.h"

#include "platform_win.h"

namespace {

const char* const kCameraFile = "camera_addresses.txt";
const char* const kCameraHeader =
    "# X360Remap - learned cameras for direct aim (one line per game version).\n"
    "# titleId buildStamp imageSize yawAddr pitchAddr signX signY unit name\n"
    "# Share this file: another player with the same game version can use it as is.\n";

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == ' ' || line[line.size() - 1] == '\t'))
            line.erase(line.size() - 1);
        size_t lead = 0;
        while (lead < line.size() && (line[lead] == ' ' || line[lead] == '\t')) lead++;
        lines.push_back(line.substr(lead));
    }
    return lines;
}

std::vector<std::string> ReadCameraFile() {
    std::string text;
    plat::ReadWholeFile(plat::WorkFile(kCameraFile), text);
    return SplitLines(text);
}

bool WriteCameraFile(const std::vector<std::string>& lines) {
    std::string out;
    bool hasComment = false;
    for (size_t i = 0; i < lines.size(); i++) hasComment = hasComment || (!lines[i].empty() && lines[i][0] == '#');
    if (!hasComment) out = kCameraHeader;
    for (size_t i = 0; i < lines.size(); i++) {
        if (lines[i].empty()) continue;
        out += lines[i];
        out += "\n";
    }
    return plat::WriteWholeFile(plat::WorkFile(kCameraFile), out);
}

// Same rule as the plugin's SaveLearnedCamera: same game AND same build.
bool SameBuild(const LearnedCamera& a, const LearnedCamera& b) {
    return a.titleId == b.titleId && a.timeStamp == b.timeStamp && a.imageSize == b.imageSize && a.version == b.version;
}

}  // namespace

void GameCameras(uint32_t titleId, GameCameraInfo& out) {
    out.learned.clear();
    out.builtin.clear();
    const std::vector<std::string> lines = ReadCameraFile();
    for (size_t i = 0; i < lines.size(); i++) {
        LearnedCamera cam;
        if (CamParseLine(lines[i].c_str(), &cam) && cam.titleId == titleId) out.learned.push_back(lines[i]);
    }
    for (size_t i = 0; i < kCamKnownCount; i++) {
        LearnedCamera cam;
        if (CamParseLine(kCamKnownLines[i], &cam) && cam.titleId == titleId) out.builtin.push_back(kCamKnownLines[i]);
    }
}

int ImportCameraLines(const std::string& text, uint32_t onlyTitleId, std::string& err) {
    err.clear();
    const std::vector<std::string> pasted = SplitLines(text);
    std::vector<std::string> good;
    std::vector<LearnedCamera> cams;
    for (size_t i = 0; i < pasted.size(); i++) {
        const std::string& line = pasted[i];
        if (line.empty() || line[0] == '#') continue;
        LearnedCamera cam;
        if (!CamParseLine(line.c_str(), &cam)) { err = line; return 0; }
        if (onlyTitleId != 0 && cam.titleId != onlyTitleId) { err = line; return 0; }
        good.push_back(line);
        cams.push_back(cam);
    }
    if (good.empty()) return 0;

    std::vector<std::string> lines = ReadCameraFile();
    for (size_t g = 0; g < good.size(); g++) {
        bool replaced = false;
        for (size_t i = 0; i < lines.size() && !replaced; i++) {
            LearnedCamera old;
            if (CamParseLine(lines[i].c_str(), &old) && SameBuild(old, cams[g])) { lines[i] = good[g]; replaced = true; }
        }
        if (!replaced) lines.push_back(good[g]);
    }
    if (!WriteCameraFile(lines)) { err = "write failed"; return 0; }
    ModelRefreshGames();
    return (int)good.size();
}

int ImportFoundCameras(uint32_t titleId, std::string& err) {
    err.clear();
    const char* local = getenv("LOCALAPPDATA");
    std::string text, mine;
    if (!local || !plat::ReadWholeFile(std::string(local) + "\\X360Remap\\found_cameras.txt", text)) return 0;
    const std::vector<std::string> lines = SplitLines(text);
    for (size_t i = 0; i < lines.size(); i++) {
        LearnedCamera cam;
        if (CamParseLine(lines[i].c_str(), &cam) && cam.titleId == titleId) mine += lines[i] + "\n";
    }
    return mine.empty() ? 0 : ImportCameraLines(mine, titleId, err);
}

int RemoveGameCameras(uint32_t titleId) {
    const std::vector<std::string> lines = ReadCameraFile();
    std::vector<std::string> keep;
    int removed = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        LearnedCamera cam;
        if (CamParseLine(lines[i].c_str(), &cam) && cam.titleId == titleId) { removed++; continue; }
        keep.push_back(lines[i]);
    }
    if (removed > 0) {
        WriteCameraFile(keep);
        ModelRefreshGames();
    }
    return removed;
}

// ----------------------------------------------------------------------------
// Profile file: readable, small, independent of the mouse it was made with.
//
// {
//   "x360remapProfile": 1,
//   "titleId": "415608CB", "gameName": "Modern Warfare 3",
//   "madeWith": "046D:C08B",
//   "mouseSensitivity": 12000, "invertMouseY": false, "deadzone": 0,
//   "antiDeadzone": 3500, "antiDeadzoneY": 6000, "smoothingMs": 25,
//   "stickLayout": 0, "curve": "exponential", "curveExponent": 1.6,
//   "cameras": [ "415608CB 4E77262A ... deg Modern Warfare 3" ]
// }
// Fields left out on import keep the device's own setting (same "inherit"
// values as HidDeviceProfile in mapping.h).
// ----------------------------------------------------------------------------
bool ExportGameProfile(const ModelDevice& mouse, uint32_t titleId, const std::string& gameName, const std::string& path) {
    using namespace rapidjson;
    Document doc(kObjectType);
    Document::AllocatorType& a = doc.GetAllocator();
    char buf[32];

    doc.AddMember("x360remapProfile", 1, a);
    _snprintf(buf, sizeof(buf) - 1, "%08X", (unsigned)titleId); buf[sizeof(buf) - 1] = 0;
    doc.AddMember("titleId", Value(buf, a), a);
    doc.AddMember("gameName", Value(gameName.c_str(), a), a);

    if (mouse.valid) {
        _snprintf(buf, sizeof(buf) - 1, "%04X:%04X", mouse.vid, mouse.pid); buf[sizeof(buf) - 1] = 0;
        doc.AddMember("madeWith", Value(buf, a), a);
        const std::vector<MouseProfileInfo> profiles = ListMouseProfiles(mouse.vid, mouse.pid, ModelConfigPath().c_str());
        ModelOwnWriteDone();
        for (size_t i = 0; i < profiles.size(); i++) {
            const MouseProfileInfo& p = profiles[i];
            if (p.titleId != titleId) continue;
            if (p.mouseSensitivity != 0) doc.AddMember("mouseSensitivity", p.mouseSensitivity, a);
            if (p.invertMouseY != -1)    doc.AddMember("invertMouseY", p.invertMouseY != 0, a);
            if (p.deadzone != -1)        doc.AddMember("deadzone", p.deadzone, a);
            if (p.antiDeadzone >= 0)     doc.AddMember("antiDeadzone", p.antiDeadzone, a);
            if (p.antiDeadzoneY >= 0)    doc.AddMember("antiDeadzoneY", p.antiDeadzoneY, a);
            if (p.smoothingMs >= 0)      doc.AddMember("smoothingMs", p.smoothingMs, a);
            doc.AddMember("stickLayout", (int)p.stickLayout, a);
            if (p.curveType == CURVE_EXPONENTIAL) {
                doc.AddMember("curve", "exponential", a);
                doc.AddMember("curveExponent", p.curveExponent, a);
            } else if (p.curveType == CURVE_LINEAR) {
                doc.AddMember("curve", "linear", a);
            }
        }
    }

    GameCameraInfo cams;
    GameCameras(titleId, cams);
    Value lines(kArrayType);
    for (size_t i = 0; i < cams.learned.size(); i++) lines.PushBack(Value(cams.learned[i].c_str(), a), a);
    doc.AddMember("cameras", lines, a);

    StringBuffer sb;
    PrettyWriter<StringBuffer> writer(sb);
    doc.Accept(writer);
    return plat::WriteWholeFile(path, std::string(sb.GetString(), sb.GetSize()) + "\n");
}

bool ImportGameProfile(const ModelDevice& mouse, const std::string& path, uint32_t& titleIdOut, std::string& err) {
    using namespace rapidjson;
    titleIdOut = 0;
    std::string text;
    if (!plat::ReadWholeFile(path, text)) { err = "file not readable"; return false; }
    Document doc;
    doc.Parse(text.c_str());
    if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("x360remapProfile") ||
        !doc.HasMember("titleId") || !doc["titleId"].IsString()) {
        err = "not an X360Remap profile file";
        return false;
    }
    const uint32_t tid = (uint32_t)strtoul(doc["titleId"].GetString(), nullptr, 16);
    if (tid == 0) { err = "invalid Title ID"; return false; }

    // Camera lines first: all checked before anything is written.
    std::string cameraText;
    if (doc.HasMember("cameras") && doc["cameras"].IsArray()) {
        const Value& arr = doc["cameras"];
        for (SizeType i = 0; i < arr.Size(); i++) {
            if (!arr[i].IsString()) { err = "invalid camera line"; return false; }
            LearnedCamera cam;
            if (!CamParseLine(arr[i].GetString(), &cam) || cam.titleId != tid) { err = arr[i].GetString(); return false; }
            cameraText += arr[i].GetString();
            cameraText += "\n";
        }
    }

    const bool hasSettings = doc.HasMember("mouseSensitivity") || doc.HasMember("deadzone") || doc.HasMember("antiDeadzone") ||
                             doc.HasMember("smoothingMs") || doc.HasMember("stickLayout") || doc.HasMember("curve") ||
                             doc.HasMember("invertMouseY") || doc.HasMember("antiDeadzoneY");
    if (hasSettings) {
        if (!mouse.valid) { err = "no mouse selected"; return false; }
        // Values not in the file: the device's own setting, read from the
        // working copy, exactly what the page would show for a new profile.
        int sens = 10000, dz = 0, adz = 0, adzY = 0, smooth = 0;
        bool inv = false;
        LoadMappingsFromFile(ModelConfigPath());
        if (const HidDeviceMapping* m = FindMapping(mouse.vid, mouse.pid)) {
            if (m->mouseSensitivity > 0) sens = m->mouseSensitivity;
            dz = m->deadzone; adz = m->antiDeadzone; adzY = m->antiDeadzoneY; smooth = m->smoothingMs; inv = m->invertMouseY;
        }
        if (doc.HasMember("mouseSensitivity") && doc["mouseSensitivity"].IsInt()) sens = doc["mouseSensitivity"].GetInt();
        if (doc.HasMember("invertMouseY") && doc["invertMouseY"].IsBool()) inv = doc["invertMouseY"].GetBool();
        if (doc.HasMember("deadzone") && doc["deadzone"].IsInt()) dz = doc["deadzone"].GetInt();
        if (doc.HasMember("antiDeadzone") && doc["antiDeadzone"].IsInt()) adz = doc["antiDeadzone"].GetInt();
        if (doc.HasMember("antiDeadzoneY") && doc["antiDeadzoneY"].IsInt()) adzY = doc["antiDeadzoneY"].GetInt();
        if (doc.HasMember("smoothingMs") && doc["smoothingMs"].IsInt()) smooth = doc["smoothingMs"].GetInt();
        int layout = 0;
        if (doc.HasMember("stickLayout") && doc["stickLayout"].IsInt()) layout = doc["stickLayout"].GetInt();
        if (layout < 0 || layout >= STICKS_LAYOUT_COUNT) layout = 0;
        uint8_t curveType = CURVE_LINEAR;
        float curveExp = 1.5f;
        if (doc.HasMember("curve") && doc["curve"].IsString() && strcmp(doc["curve"].GetString(), "exponential") == 0) {
            curveType = CURVE_EXPONENTIAL;
            if (doc.HasMember("curveExponent") && doc["curveExponent"].IsNumber()) curveExp = doc["curveExponent"].GetFloat();
            if (curveExp < 1.0f || curveExp > 4.0f) curveExp = 1.5f;
        }
        if (sens < 1000 || sens > 30000 || dz < 0 || dz > 32767 || adz < 0 || adz > 32767 || adzY < 0 || adzY > 32767 ||
            smooth < 0 || smooth > 200) {
            err = "a value is out of range";
            return false;
        }
        const char* name = (doc.HasMember("gameName") && doc["gameName"].IsString()) ? doc["gameName"].GetString() : nullptr;
        const bool ok = ApplyMouseProfileFull(mouse.vid, mouse.pid, tid, sens, inv, dz, curveType, curveExp, name, adz,
                                              ModelConfigPath().c_str(), layout, adzY, smooth);
        ModelOwnWriteDone();
        if (!ok) { err = "the profile could not be saved"; return false; }
    }
    if (!cameraText.empty()) {
        std::string camErr;
        ImportCameraLines(cameraText, tid, camErr);
        if (!camErr.empty()) { err = camErr; return false; }
    }
    titleIdOut = tid;
    ModelRefreshGames();
    return true;
}
