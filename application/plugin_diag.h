#pragma once
// Why is the plugin not running? (2026-10-20, tester: "Plugin silent", no
// keyboard / mouse, even after going back to 1.2 - the launch.ini line pointed
// to a file that no longer existed.) Reads DashLaunch's launch.ini text and
// says what is wrong. Header only, no console API: tested with g++
// (application/tests/test_plugin_diag.cpp).
#include <string>
#include <cctype>

enum PluginDiag {
    DIAG_NO_INI = 0,       // no launch.ini found
    DIAG_NO_LINE,          // no X360Remap.xex line in [Plugins]
    DIAG_FILE_MISSING,     // the line points to a file that does not exist
    DIAG_HIDDRIVER,        // hiddriver360 is loaded too
    DIAG_LINE_OK           // the line looks right: other causes (dashboard, tray, xbdm)
};

struct PluginDiagResult {
    PluginDiag  diag;
    std::string pluginPath;    // value of the X360Remap.xex line, as written
    std::string localPath;     // the same, as the Studio can open it ("HDD:\..."), or empty
    std::string otherPath;     // hiddriver line, if any
};

inline std::string PdLower(const std::string& s) {
    std::string r(s);
    for (size_t i = 0; i < r.size(); i++) r[i] = (char)tolower((unsigned char)r[i]);
    return r;
}
inline std::string PdTrim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '"')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '"')) b--;
    return s.substr(a, b - a);
}

// DashLaunch path -> path the Studio can open. Only the hard drive is mapped
// ("Hdd:\..." or "\Device\Harddisk0\Partition1\..."); anything else -> "".
inline std::string PdLocalPath(const std::string& p) {
    const std::string l = PdLower(p);
    if (l.compare(0, 4, "hdd:") == 0) return "HDD:" + p.substr(4);
    const std::string dev = "\\device\\harddisk0\\partition1";
    if (l.compare(0, dev.size(), dev) == 0) return "HDD:" + p.substr(dev.size());
    return "";
}

// fileExists: callback (path) -> bool, so the test needs no disk.
template <class FileExists>
PluginDiagResult DiagnoseLaunchIni(const std::string* ini, FileExists fileExists) {
    PluginDiagResult r;
    r.diag = DIAG_NO_INI;
    if (!ini) return r;
    r.diag = DIAG_NO_LINE;
    bool inPlugins = false;
    size_t pos = 0;
    while (pos <= ini->size()) {
        size_t e = ini->find('\n', pos);
        if (e == std::string::npos) e = ini->size();
        std::string line = ini->substr(pos, e - pos);
        pos = e + 1;
        const size_t c = line.find_first_of(";#");
        if (c != std::string::npos) line = line.substr(0, c);
        line = PdTrim(line);
        if (line.empty()) continue;
        if (line[0] == '[') { inPlugins = PdLower(line).find("[plugins]") == 0; continue; }
        if (!inPlugins) continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = PdLower(PdTrim(line.substr(0, eq)));
        const std::string val = PdTrim(line.substr(eq + 1));
        if (key.compare(0, 6, "plugin") != 0 || val.empty()) continue;
        const std::string lv = PdLower(val);
        const size_t slash = lv.find_last_of("\\/:");
        const std::string base = slash == std::string::npos ? lv : lv.substr(slash + 1);
        if (base == "x360remap.xex" && r.pluginPath.empty()) r.pluginPath = val;
        else if (lv.find("hiddriver") != std::string::npos && r.otherPath.empty()) r.otherPath = val;
    }
    if (r.pluginPath.empty()) return r;           // DIAG_NO_LINE
    r.localPath = PdLocalPath(r.pluginPath);
    if (!r.localPath.empty() && !fileExists(r.localPath)) { r.diag = DIAG_FILE_MISSING; return r; }
    if (!r.otherPath.empty()) { r.diag = DIAG_HIDDRIVER; return r; }
    r.diag = DIAG_LINE_OK;
    return r;
}
