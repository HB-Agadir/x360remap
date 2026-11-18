#include "install_win.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

const char* const kPluginDashPath = "Hdd:\\X360Remap.xex";
const char* const kAuroraScriptPath = "/Game/User/Scripts/Utility/X360Remap Setup/Main.lua";

namespace {

std::string Lower(const std::string& s) { return PdLower(s); }

bool EndsWith(const std::string& s, const char* tail) {
    const size_t n = strlen(tail);
    return s.size() >= n && Lower(s.substr(s.size() - n)) == tail;
}

// "/Hdd1/X360RemapStudio/" -> "/Hdd1"
std::string DriveOf(const std::string& dir) {
    const std::string::size_type p = dir.find('/', 1);
    return p == std::string::npos ? dir : dir.substr(0, p);
}

// "HDD:\a\b.xex" (PdLocalPath form) -> "/Hdd1/a/b.xex"
std::string FtpFromLocal(const std::string& drive, const std::string& local) {
    std::string rest = local.size() > 4 ? local.substr(4) : std::string();
    for (size_t i = 0; i < rest.size(); i++) if (rest[i] == '\\') rest[i] = '/';
    if (rest.empty() || rest[0] != '/') rest = "/" + rest;
    return drive + rest;
}

bool HasName(const std::vector<std::string>& names, const std::string& name) {
    for (size_t i = 0; i < names.size(); i++)
        if (Lower(names[i]) == Lower(name)) return true;
    return false;
}

// Lines with their end of line kept, so the text can be put back unchanged.
std::vector<std::string> SplitKeep(const std::string& s) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t e = s.find('\n', pos);
        e = (e == std::string::npos) ? s.size() : e + 1;
        out.push_back(s.substr(pos, e - pos));
        pos = e;
    }
    return out;
}

std::string EolOf(const std::string& line) {
    if (line.size() >= 2 && line.compare(line.size() - 2, 2, "\r\n") == 0) return "\r\n";
    if (!line.empty() && line[line.size() - 1] == '\n') return "\n";
    return "";
}

// "plugin2 = x ; note\r\n" -> key "plugin2", value "x" (comment and quotes removed)
bool ParseKeyValue(const std::string& raw, std::string& key, std::string& value) {
    std::string line = raw.substr(0, raw.size() - EolOf(raw).size());
    const size_t c = line.find_first_of(";#");
    if (c != std::string::npos) line = line.substr(0, c);
    const size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    key = Lower(PdTrim(line.substr(0, eq)));
    value = PdTrim(line.substr(eq + 1));
    return true;
}

}  // namespace

std::string PluginFtpPath(const std::string& studioDir) { return DriveOf(studioDir) + "/X360Remap.xex"; }

bool ProposeLaunchIniFix(const std::string& ini, const std::string& currentValue, const std::string& newValue,
                         std::string& oldLine, std::string& newLine, std::string& fixedIni) {
    oldLine.clear();
    newLine.clear();
    std::vector<std::string> lines = SplitKeep(ini);
    std::string eol = "\r\n";
    for (size_t i = 0; i < lines.size(); i++) if (!EolOf(lines[i]).empty()) { eol = EolOf(lines[i]); break; }

    int sectionStart = -1, sectionEnd = -1, emptySlot = -1, valueLine = -1;
    bool used[10] = { false };
    for (size_t i = 0; i < lines.size(); i++) {
        const std::string t = PdTrim(lines[i]);
        if (!t.empty() && t[0] == '[') {
            if (sectionStart >= 0 && sectionEnd < 0) sectionEnd = (int)i;
            if (Lower(t).find("[plugins]") == 0) sectionStart = (int)i;
            continue;
        }
        if (sectionStart < 0 || sectionEnd >= 0) continue;
        std::string key, value;
        if (!ParseKeyValue(lines[i], key, value) || key.compare(0, 6, "plugin") != 0) continue;
        const int n = atoi(key.c_str() + 6);
        if (n > 0 && n < 10) used[n] = true;
        if (value.empty() && emptySlot < 0) emptySlot = (int)i;
        if (!currentValue.empty() && value == currentValue && valueLine < 0) valueLine = (int)i;
    }
    if (sectionStart >= 0 && sectionEnd < 0) sectionEnd = (int)lines.size();

    if (valueLine >= 0 || emptySlot >= 0) {
        // Keep "plugin1 =" exactly as written, replace only the value.
        const int i = valueLine >= 0 ? valueLine : emptySlot;
        const std::string lineEol = EolOf(lines[i]);
        std::string body = lines[i].substr(0, lines[i].size() - lineEol.size());
        const size_t eq = body.find('=');
        oldLine = body;
        newLine = body.substr(0, eq + 1) + " " + newValue;
        if (PdTrim(oldLine) == PdTrim(newLine)) return false;
        lines[i] = newLine + lineEol;
    } else {
        // DashLaunch reads plugin1 to plugin5 only: a plugin6 line would be ignored.
        int n = 1;
        while (n <= 5 && used[n]) n++;
        if (n > 5) return false;
        char key[16];
        sprintf(key, "plugin%d", n);
        newLine = std::string(key) + " = " + newValue;
        if (sectionStart < 0) {
            // No [Plugins] section at all: added at the end.
            if (!lines.empty() && EolOf(lines.back()).empty()) lines.back() += eol;
            lines.push_back("[Plugins]" + eol);
            lines.push_back(newLine + eol);
        } else {
            // After the last non-empty line of the section.
            int at = sectionEnd;
            while (at - 1 > sectionStart && PdTrim(lines[at - 1].substr(0, lines[at - 1].size() - EolOf(lines[at - 1]).size())).empty()) at--;
            if (EolOf(lines[at - 1]).empty()) lines[at - 1] += eol;
            lines.insert(lines.begin() + at, newLine + eol);
        }
    }
    fixedIni.clear();
    for (size_t i = 0; i < lines.size(); i++) fixedIni += lines[i];
    return true;
}

bool CheckInstall(FtpClient& ftp, const std::string& studioDir, InstallCheck& out, std::string& err) {
    out = InstallCheck();
    const std::string drive = DriveOf(studioDir);

    std::vector<std::string> names;
    if (!ftp.ListNames(drive + "/", names, err)) return false;
    out.pluginPresent = HasName(names, "X360Remap.xex");
    if (!ftp.ListNames(studioDir, names, err)) return false;
    out.studioAppPresent = HasName(names, "X360RemapStudio.xex");

    // Same places as the console Studio (application/main.cpp).
    const char* inis[] = { "/launch.ini", "/DashLaunch/launch.ini" };
    for (size_t i = 0; i < 2 && !out.iniFound; i++) {
        bool found = false;
        std::string text;
        if (!ftp.Download(drive + inis[i], text, found, err)) return false;
        if (found && !text.empty()) { out.iniFound = true; out.iniFtpPath = drive + inis[i]; out.iniText = text; }
    }

    bool listError = false;
    struct Exists {
        FtpClient* ftp; std::string drive; bool* listError;
        bool operator()(const std::string& local) const {
            const std::string path = FtpFromLocal(drive, local);
            const std::string::size_type slash = path.find_last_of('/');
            std::vector<std::string> names;
            std::string e;
            if (!ftp->ListNames(path.substr(0, slash + 1), names, e)) { *listError = true; return true; }
            return HasName(names, path.substr(slash + 1));
        }
    };
    Exists exists = { &ftp, drive, &listError };
    out.diag = DiagnoseLaunchIni(out.iniFound ? &out.iniText : nullptr, exists);
    if (listError) { err = "could not list a folder of the console"; return false; }

    // Aurora's settings.db (read only): a scan path "\X360RemapStudio".
    // shortcut: a byte search, not a SQLite read - a deleted row left in a free
    // page would read as present; read the ScanPaths table if that ever matters.
    {
        std::string db;
        bool found = false;
        std::string e;
        if (ftp.Download("/Game/Data/Databases/settings.db", db, found, e) && found && !db.empty()) {
            out.auroraChecked = true;
            out.auroraHasStudio = Lower(db).find("\\x360remapstudio") != std::string::npos;
        }
    }

    // Offer the line only once the plugin is at the root of the drive, and
    // only when launch.ini does not already point there.
    if (out.iniFound && out.pluginPresent &&
        Lower(out.diag.pluginPath) != Lower(kPluginDashPath))
    {
        out.fixOffered = ProposeLaunchIniFix(out.iniText, out.diag.pluginPath, kPluginDashPath,
                                             out.fixOldLine, out.fixNewLine, out.fixedIni);
        // No X360Remap line to change and no free slot: another plugin would
        // have to go, and that is the user's choice, never the app's.
        out.slotsFull = !out.fixOffered && out.diag.pluginPath.empty();
    }
    return true;
}

SafeWriteOutcome WriteLaunchIni(FtpClient& ftp, const std::string& iniFtpPath, const std::string& expected,
                                const std::string& fixedIni) {
    // The one write outside X360RemapStudio\: DashLaunch's launch.ini only.
    if (iniFtpPath.find("..") != std::string::npos || !(EndsWith(iniFtpPath, "/launch.ini") &&
        (std::count(iniFtpPath.begin(), iniFtpPath.end(), '/') == 2 || EndsWith(iniFtpPath, "/dashlaunch/launch.ini")))) {
        SafeWriteOutcome out;
        out.err = "refused: not DashLaunch's launch.ini";
        return out;
    }
    // hasBase + expected: if launch.ini changed since the check, nothing is written.
    return SafeWriteAt(ftp, iniFtpPath, fixedIni, true, expected, false);
}

std::string AuroraSetupScript() {
    // Plain ASCII, like every Aurora script. The insert takes the hard drive's
    // id from Aurora's own list of devices, and does nothing if the path is
    // already there.
    return
        "scriptTitle = \"X360Remap Setup\"\n"
        "scriptAuthor = \"X360Remap\"\n"
        "scriptVersion = 1\n"
        "scriptDescription = \"Adds the X360RemapStudio folder to Aurora's applications. Run it once.\"\n"
        "scriptPermissions = { \"sql\" }\n"
        "\n"
        "function main()\n"
        "\tlocal found = false\n"
        "\tfor _, row in pairs(Sql.ExecuteFetchRows(\"SELECT Id FROM Config.ScanPaths WHERE Path = '\\\\X360RemapStudio'\")) do\n"
        "\t\tfound = true\n"
        "\tend\n"
        "\tif not found then\n"
        "\t\tSql.Execute(\"INSERT INTO Config.ScanPaths (Path, DeviceId, Depth, ScriptData, OptionsFlag) "
        "SELECT '\\\\X360RemapStudio', DeviceId, 2, 'Applications', 1 FROM MountedDevices WHERE DeviceName = 'Hdd1'\")\n"
        "\tend\n"
        "\tlocal answer = Script.ShowMessageBox(\"X360Remap Setup\", \"X360Remap Studio is now in your applications. "
        "Restart Aurora to see it?\", \"Yes\", \"No\")\n"
        "\tif answer.Button == 1 then\n"
        "\t\tAurora.Restart()\n"
        "\tend\n"
        "end\n";
}

bool InstallAuroraScript(FtpClient& ftp, std::string& err) {
    // The one write in Aurora's folder (CONTRACT.md "Outside the folder"):
    // this fixed path only, after the user asked for it.
    const char* dirs[] = { "/Game/User", "/Game/User/Scripts", "/Game/User/Scripts/Utility", "/Game/User/Scripts/Utility/X360Remap Setup" };
    for (size_t i = 0; i < 4; i++)
        if (!ftp.MakeDir(dirs[i], err)) return false;
    const SafeWriteOutcome o = SafeWriteAt(ftp, kAuroraScriptPath, AuroraSetupScript(), false, std::string(), true);
    if (o.result == SAFE_ERROR) { err = o.err; return false; }
    return true;
}

bool DownloadStudioSettings(FtpClient& ftp, const std::string& studioDir,
                            std::vector<std::pair<std::string, std::string> >& files, std::string& err) {
    files.clear();
    std::vector<std::string> names;
    if (!ftp.ListNames(studioDir, names, err)) return false;
    for (size_t i = 0; i < names.size(); i++) {
        const std::string& n = names[i];
        if (EndsWith(n, ".xex") || EndsWith(n, ".new") || !IsPlainFileName(n)) continue;
        std::string data;
        bool found = false;
        if (!ftp.Download(JoinRemote(studioDir, n), data, found, err)) return false;
        if (found) files.push_back(std::make_pair(n, data));   // folders answer "not found"
    }
    return true;
}
