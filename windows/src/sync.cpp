#include "sync.h"

#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "install_win.h"
#include "update_win.h"
#include <rapidjson/document.h>
#include "model_win.h"
#include "platform_win.h"
#include "win_i18n.h"
#include "../../X360Remap/camera_learn.h"
#include "../../application/aurora_library.h"
#include "../../application/overlay_settings.h"

namespace consync {

const char* const kWritableFiles[3] = { "X360Remap.json", "camera_addresses.txt", "overlay_settings.txt" };
const int kWritableCount = 3;
const char* const kLogFiles[4] = {
    "X360Remap_plugin.log", "X360Remap_plugin_previous.log",
    "X360Remap_studio.log", "X360Remap_studio_previous.log"
};

namespace {

const char* const kReadOnlyFiles[] = {
    "known_devices.txt", "known_titles.txt", "X360Remap_switches.txt",
    "X360Remap_plugin.log", "X360Remap_plugin_previous.log",
    "X360Remap_studio.log", "X360Remap_studio_previous.log"
};
const int kReadOnlyCount = (int)(sizeof(kReadOnlyFiles) / sizeof(kReadOnlyFiles[0]));
const char* const kReloadLine = "mappings reloaded";

enum JobType { JOB_PULL, JOB_PUSH, JOB_LOGS, JOB_SWITCH, JOB_SCAN, JOB_COVER,
               JOB_CHECK_INSTALL, JOB_BACKUP, JOB_INSTALL, JOB_FIX_INI,
               JOB_CHECK_UPDATE, JOB_DOWNLOAD_UPDATE, JOB_AURORA_SCRIPT, JOB_REBOOT, JOB_STATUS };

struct PushFile {
    std::string name, local, base;
    bool hasBase, force;
};

struct Job {
    JobType     type;
    WinSettings cfg;
    std::vector<PushFile> files;       // JOB_PUSH
    std::string switchName;            // JOB_SWITCH
    bool        switchOn;
    bool        wantAurora;            // JOB_PULL: look for Aurora's database
    std::string auroraRoot;            // JOB_COVER
    std::vector<std::string> gameDataDirs;
    uint32_t    titleId;
    std::string pluginXex, studioXex;  // JOB_INSTALL
    InstallCheck shown;                // JOB_FIX_INI: the check whose change the user accepted
    ReleaseInfo release;               // JOB_DOWNLOAD_UPDATE
    Job() : type(JOB_PULL), switchOn(false), wantAurora(false), titleId(0) { memset(&cfg, 0, sizeof(cfg)); }
};

struct FileData {
    std::string name, data;
    bool found;
};

struct PushedFile {
    std::string      name, local;
    SafeWriteOutcome out;
};

struct JobResult {
    JobType     type;
    bool        ok;
    std::string err;
    std::vector<FileData>    files;
    std::vector<PushedFile>  pushed;
    std::vector<std::string> remoteNames;
    bool        haveNames;
    int         reloadSeen;            // 0 unknown, 1 the plugin logged the reload, 2 not seen
    std::vector<FoundFtp> found;
    bool        connectFailed;         // nothing answered at the saved address
    bool        auroraFound;
    std::string auroraRoot, contentDb;
    std::vector<std::string> gameDataDirs;
    uint32_t    titleId;
    std::string cover;
    std::string status;                // JOB_STATUS: X360Remap_status.json ("" = none)
    bool        installChecked;
    InstallCheck install;
    int         logActive;             // -1 unknown, 0 log did not grow, 1 grew
    std::string backupDir;             // where the settings were saved on the PC
    int         backupCount;
    bool        launchIniWritten;
    bool        auroraScriptWritten;
    int         reboot;                // JOB_REBOOT: RebootState
    std::string pluginVersionAfter;
    ReleaseInfo release;
    std::string pluginXex, studioXex;  // JOB_DOWNLOAD_UPDATE
    JobResult() : type(JOB_PULL), ok(false), haveNames(false), reloadSeen(0), connectFailed(false), auroraFound(false), titleId(0),
                  installChecked(false), logActive(-1), backupCount(0), launchIniWritten(false), auroraScriptWritten(false), reboot(0) {}
};

// --- shared with the worker ----------------------------------------------------
std::mutex              g_mutex;
std::condition_variable g_wake;
std::deque<Job>         g_jobs;
std::deque<JobResult>   g_results;
bool                    g_quit = false;
int                     g_running = 0;       // jobs queued or running (covers excluded)
std::thread             g_thread;

// --- main thread only -------------------------------------------------------------
LinkState   g_state = LINK_OFFLINE;
std::string g_lastError, g_pluginVersion;
int         g_dashboardBuild = 0;
std::string g_studioVersion;
bool        g_dirty = false;
double      g_lastExchange = -1.0;
std::vector<Conflict>    g_conflicts;
bool        g_repushAfterConflicts = false;
std::vector<std::string> g_remoteNames;
bool        g_scanRunning = false;
std::vector<FoundFtp>    g_scanResults;
std::string              g_refindFor;             // address already searched for once
bool                     g_refindPending = false; // the running scan is that search
bool        g_auroraFound = false, g_auroraTried = false;
std::string g_auroraRoot;
std::vector<std::string> g_gameDataDirs;
uint32_t    g_auroraGeneration = 0;
std::vector<uint32_t>    g_coversAsked;
InstallState g_install;
UpdateState  g_update;

int CountLines(const std::string& text, const char* needle) {
    int n = 0;
    std::string::size_type p = 0;
    while ((p = text.find(needle, p)) != std::string::npos) { n++; p++; }
    return n;
}

// ============================================================================
// Worker
// ============================================================================

bool Connect(FtpClient& ftp, const WinSettings& cfg, std::string& err) {
    return ftp.Connect(cfg.consoleIp, cfg.ftpPort, cfg.ftpUser, cfg.ftpPassword, err);
}

bool Fetch(FtpClient& ftp, const std::string& dir, const char* name, JobResult& r) {
    FileData f;
    f.name = name;
    f.found = false;
    if (!ftp.Download(JoinRemote(dir, name), f.data, f.found, r.err)) return false;
    r.files.push_back(f);
    return true;
}

// "/Hdd1/X360RemapStudio/" -> "/Hdd1"
std::string DriveOf(const std::string& remoteDir) {
    const std::string::size_type p = remoteDir.find('/', 1);
    return p == std::string::npos ? remoteDir : remoteDir.substr(0, p);
}

void FindAurora(FtpClient& ftp, const std::string& drive, JobResult& r) {
    // Same order as the console Studio (FindAuroraRoot in application/main.cpp): first the
    // dashboard DashLaunch starts (launch.ini Default=, e.g. HDD:\App\Aurora 0.7b.2 on HB's
    // console, 2026-11-18: Games stayed empty), then the known places.
    std::vector<std::string> roots;
    const char* inis[] = { "/launch.ini", "/DashLaunch/launch.ini" };
    for (size_t i = 0; i < sizeof(inis) / sizeof(inis[0]); i++) {
        std::string ini, dir, err;
        bool found = false;
        if (!ftp.Download(drive + inis[i], ini, found, err) || !found) continue;
        if (!DashboardDirFromLaunchIni(ini, dir) || dir.size() < 5 || dir.compare(0, 4, "HDD:") != 0) continue;
        std::string rest = dir.substr(4);   // "\App\Aurora 0.7b.2" -> "/App/Aurora 0.7b.2"
        for (size_t k = 0; k < rest.size(); k++) if (rest[k] == '\\') rest[k] = '/';
        if (rest.empty() || rest[0] != '/') rest = "/" + rest;
        roots.push_back(drive + rest);
    }
    const char* known[] = { "/App/Aurora", "/Apps/Aurora", "/Aurora", "/Aurora0.7b", "/Dashboards/Aurora", "/Homebrew/Aurora" };
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) roots.push_back(drive + known[i]);
    for (size_t i = 0; i < roots.size(); i++) {
        const std::string& root = roots[i];
        bool found = false;
        std::string err;
        if (!ftp.Download(root + "/Data/Databases/content.db", r.contentDb, found, err)) return;
        if (!found) continue;
        r.auroraFound = true;
        r.auroraRoot = root;
        ftp.ListNames(root + "/Data/GameData", r.gameDataDirs, err);
        return;
    }
}

// Settings of the console copied to DataDir\backups\<date time>\ (written
// here, before anything is replaced on the console).
bool BackupToPc(FtpClient& ftp, const std::string& dir, JobResult& r) {
    std::vector<std::pair<std::string, std::string> > files;
    if (!DownloadStudioSettings(ftp, dir, files, r.err)) return false;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char stamp[32];
    _snprintf(stamp, sizeof(stamp) - 1, "%04d-%02d-%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    stamp[sizeof(stamp) - 1] = 0;
    r.backupDir = plat::DataDir() + "backups\\" + stamp + "\\";
    CreateDirectoryA((plat::DataDir() + "backups").c_str(), nullptr);
    CreateDirectoryA(r.backupDir.c_str(), nullptr);
    for (size_t i = 0; i < files.size(); i++) {
        if (!plat::WriteWholeFile(r.backupDir + files[i].first, files[i].second)) {
            r.err = "could not save the backup on the PC: " + r.backupDir;
            return false;
        }
    }
    r.backupCount = (int)files.size();
    return true;
}

void RunInstallJob(FtpClient& ftp, const Job& job, const std::string& dir, JobResult& r) {
    if (job.type == JOB_BACKUP) {
        r.ok = BackupToPc(ftp, dir, r);
        return;
    }
    if (job.type == JOB_INSTALL) {
        // Nothing is replaced unless the settings are safe on the PC first.
        if (!BackupToPc(ftp, dir, r)) return;
        // Plugin at the root of the drive (a fixed path, nothing else is
        // written there), Studio in X360RemapStudio\.
        SafeWriteOutcome o = SafeWriteAt(ftp, PluginFtpPath(dir), job.pluginXex, false, std::string(), true);
        if (o.result == SAFE_ERROR) { r.err = "X360Remap.xex: " + o.err; return; }
        o = SafeWriteFile(ftp, dir, "X360RemapStudio.xex", job.studioXex, false, std::string(), true);
        if (o.result == SAFE_ERROR) { r.err = "X360RemapStudio.xex: " + o.err; return; }
    }
    if (job.type == JOB_AURORA_SCRIPT) {
        if (!InstallAuroraScript(ftp, r.err)) return;
        r.auroraScriptWritten = true;
    }
    if (job.type == JOB_FIX_INI) {
        const SafeWriteOutcome o = WriteLaunchIni(ftp, job.shown.iniFtpPath, job.shown.iniText, job.shown.fixedIni);
        if (o.result == SAFE_CONFLICT) { r.err = "launch.ini changed on the console since the check: nothing written, check again"; return; }
        if (o.result == SAFE_ERROR) { r.err = o.err; return; }
        r.launchIniWritten = true;
    }
    // Check (also after an install or a fix, to show the new state).
    if (!CheckInstall(ftp, dir, r.install, r.err)) return;
    r.installChecked = true;
    // "Plugin alive": its log grows while it runs.
    if (job.type == JOB_CHECK_INSTALL) {
        FileData a, b;
        a.name = b.name = kLogFiles[0];
        std::string e;
        if (ftp.Download(JoinRemote(dir, kLogFiles[0]), a.data, a.found, e) && a.found) {
            Sleep(3000);
            if (ftp.Download(JoinRemote(dir, kLogFiles[0]), b.data, b.found, e) && b.found) {
                r.logActive = (b.data != a.data) ? 1 : 0;
                r.files.push_back(b);
            }
        }
        FileData studio;
        studio.name = kLogFiles[2];
        if (ftp.Download(JoinRemote(dir, kLogFiles[2]), studio.data, studio.found, e) && studio.found) r.files.push_back(studio);
    }
    r.ok = true;
}

// Cold reboot, then wait for the console and for a NEW plugin log: the plugin
// truncates its log at every boot, so a log that differs from the one before
// the reboot and holds the start line = the plugin was loaded again.
void RunReboot(const Job& job, JobResult& r) {
    std::string before, e;
    bool found = false;
    {
        FtpClient ftp;
        if (ftp.Connect(job.cfg.consoleIp, job.cfg.ftpPort, job.cfg.ftpUser, job.cfg.ftpPassword, e))
            ftp.Download(JoinRemote(job.cfg.remoteDir, kLogFiles[0]), before, found, e);
    }
    if (!XbdmColdReboot(job.cfg.consoleIp, r.err)) { r.reboot = REBOOT_NO_XBDM; r.ok = true; return; }
    Sleep(15000);
    const DWORD start = GetTickCount();
    bool ftpBack = false;
    DWORD ftpBackAt = 0;
    while (GetTickCount() - start < 240000) {
        FtpClient ftp;
        if (ftp.Connect(job.cfg.consoleIp, job.cfg.ftpPort, job.cfg.ftpUser, job.cfg.ftpPassword, e)) {
            if (!ftpBack) { ftpBack = true; ftpBackAt = GetTickCount(); }
            std::string now;
            if (ftp.Download(JoinRemote(job.cfg.remoteDir, kLogFiles[0]), now, found, e) && found && now != before) {
                const char* marker = "X360Remap plugin v";
                const std::string::size_type p = now.find(marker);
                if (p != std::string::npos) {
                    std::string::size_type b = p + strlen(marker), q = b;
                    while (q < now.size() && now[q] != ' ' && now[q] != '\r' && now[q] != '\n') q++;
                    r.pluginVersionAfter = now.substr(b, q - b);
                    FileData f;
                    f.name = kLogFiles[0]; f.data = now; f.found = true;
                    r.files.push_back(f);
                    r.reboot = REBOOT_PLUGIN_RUNNING;
                    r.ok = true;
                    return;
                }
            }
            // FTP back for a minute and still no new plugin log: not loaded.
            if (GetTickCount() - ftpBackAt > 60000) { r.reboot = REBOOT_NO_PLUGIN; r.ok = true; return; }
        }
        Sleep(5000);
    }
    r.reboot = ftpBack ? REBOOT_NO_PLUGIN : REBOOT_NOT_BACK;
    r.ok = true;
}

void RunJob(const Job& job, JobResult& r) {
    r.type = job.type;
    r.titleId = job.titleId;
    if (job.type == JOB_SCAN) {
        ScanLocalFtp(r.found);
        r.ok = true;
        return;
    }
    if (job.type == JOB_CHECK_UPDATE) {
        r.ok = FetchNewestRelease(job.cfg.updatePreviews, r.release, r.err);
        return;
    }
    if (job.type == JOB_REBOOT) {
        RunReboot(job, r);
        return;
    }
    if (job.type == JOB_DOWNLOAD_UPDATE) {
        const ReleaseInfo& rel = job.release;
        r.release = rel;
        if (!rel.pluginUrl.empty() && !rel.studioUrl.empty()) {
            if (!HttpGet(rel.pluginUrl, r.pluginXex, r.err) || !HttpGet(rel.studioUrl, r.studioXex, r.err)) return;
        } else if (!rel.zipUrl.empty()) {
            std::string zip;
            if (!HttpGet(rel.zipUrl, zip, r.err)) return;
            char id[32];
            _snprintf(id, sizeof(id) - 1, "%lld", rel.id); id[sizeof(id) - 1] = 0;
            CreateDirectoryA(plat::CacheFile("downloads").c_str(), nullptr);
            const std::string zipPath = plat::CacheFile("downloads\\") + id + ".zip";
            const std::string dirPath = plat::CacheFile("install\\gh") + id;
            if (!plat::WriteWholeFile(zipPath, zip) || !plat::ExtractZip(zipPath, dirPath)) { r.err = "the release zip could not be opened"; return; }
            const std::string p = plat::FindFileBelow(dirPath, "X360Remap.xex"), a = plat::FindFileBelow(dirPath, "X360RemapStudio.xex");
            if (p.empty() || a.empty() || !plat::ReadWholeFile(p, r.pluginXex) || !plat::ReadWholeFile(a, r.studioXex)) {
                r.err = "this release does not hold X360Remap.xex and X360RemapStudio.xex";
                return;
            }
        } else {
            r.err = "this release has no file to install";
            return;
        }
        r.ok = !r.pluginXex.empty() && !r.studioXex.empty();
        if (!r.ok) r.err = "empty file downloaded";
        return;
    }

    FtpClient ftp;
    if (!Connect(ftp, job.cfg, r.err)) { r.connectFailed = true; return; }
    const std::string dir = job.cfg.remoteDir;
    // First install on this console: X360RemapStudio\ may not exist yet.
    if ((job.type == JOB_PUSH || job.type == JOB_SWITCH || job.type == JOB_INSTALL) && IsStudioDir(dir)) {
        std::string e;
        if (!ftp.MakeDir(dir, e)) { r.err = e; return; }
    }

    if (job.type == JOB_PULL || job.type == JOB_LOGS) {
        if (job.type == JOB_PULL)
            for (int i = 0; i < kWritableCount; i++)
                if (!Fetch(ftp, dir, kWritableFiles[i], r)) return;
        for (int i = 0; i < kReadOnlyCount; i++)
            if (!Fetch(ftp, dir, kReadOnlyFiles[i], r)) return;
        std::string ignored;
        r.haveNames = ftp.ListNames(dir, r.remoteNames, ignored);
        if (job.wantAurora) FindAurora(ftp, DriveOf(dir), r);
        r.ok = true;
        return;
    }

    if (job.type == JOB_PUSH) {
        // The plugin logs a line each time it reloads X360Remap.json: counting
        // it before and after tells the user the console really took the file.
        std::string logBefore;
        bool found = false;
        std::string ignored;
        ftp.Download(JoinRemote(dir, kLogFiles[0]), logBefore, found, ignored);
        const int reloadsBefore = CountLines(logBefore, kReloadLine);

        bool jsonSent = false;
        for (size_t i = 0; i < job.files.size(); i++) {
            const PushFile& f = job.files[i];
            PushedFile p;
            p.name = f.name;
            p.local = f.local;
            // The plugin draws only while enable_overlay.txt exists (CONTRACT.md), and reads that
            // when overlay_settings.txt changes: the switch goes first, then the settings.
            if (f.name == kWritableFiles[2]) {
                OverlaySettings shown;
                OverlayParseText(f.local.c_str(), shown);
                const std::string flag = JoinRemote(dir, "enable_overlay.txt");
                std::string flagErr;
                if (!OverlayShowsAnything(shown)) ftp.Delete(flag, flagErr);   // already absent = fine
                else if (!ftp.Upload(flag, std::string(), flagErr)) ftp.Upload(flag, std::string("1\r\n"), flagErr);
            }
            p.out = SafeWriteFile(ftp, dir, f.name, f.local, f.hasBase, f.base, f.force);
            if (p.out.result == SAFE_OK && f.name == kWritableFiles[0]) jsonSent = true;
            r.pushed.push_back(p);
            if (p.out.result == SAFE_ERROR) { r.err = p.out.err; break; }
        }
        if (jsonSent) {
            r.reloadSeen = 2;
            for (int attempt = 0; attempt < 4 && r.reloadSeen != 1; attempt++) {
                Sleep(900);
                FileData log;
                log.name = kLogFiles[0];
                if (!ftp.Download(JoinRemote(dir, kLogFiles[0]), log.data, log.found, ignored)) break;
                if (log.found && CountLines(log.data, kReloadLine) > reloadsBefore) r.reloadSeen = 1;
                if (attempt == 3 || r.reloadSeen == 1) r.files.push_back(log);
            }
        }
        r.ok = r.err.empty();
        return;
    }

    if (job.type == JOB_SWITCH) {
        // Only "enable_xxx.txt" / "disable_xxx.txt" files of the Studio folder.
        const std::string& n = job.switchName;
        const bool nameOk = IsPlainFileName(n) && IsStudioDir(dir) && n.size() > 4 &&
                            n.compare(n.size() - 4, 4, ".txt") == 0 &&
                            (n.compare(0, 7, "enable_") == 0 || n.compare(0, 8, "disable_") == 0);
        if (!nameOk) { r.err = "refused: not a switch file of the X360RemapStudio folder"; return; }
        const std::string path = JoinRemote(dir, n);
        if (job.switchOn) {
            // The plugin only tests that the file exists. Some servers refuse
            // an empty upload: a one-line file is then sent instead.
            if (!ftp.Upload(path, std::string(), r.err) && !ftp.Upload(path, std::string("1\r\n"), r.err)) return;
        } else {
            std::string ignored;
            ftp.Delete(path, ignored);   // already absent = fine
        }
        r.haveNames = ftp.ListNames(dir, r.remoteNames, r.err);
        r.ok = r.haveNames;
        return;
    }

    if (job.type == JOB_CHECK_INSTALL || job.type == JOB_BACKUP || job.type == JOB_INSTALL || job.type == JOB_FIX_INI ||
        job.type == JOB_AURORA_SCRIPT) {
        RunInstallJob(ftp, job, dir, r);
        return;
    }

    if (job.type == JOB_STATUS) {
        // The plugin writes its status only while pc_link.flag keeps CHANGING (CONTRACT.md):
        // a new content each time, then the status of the last 2 s.
        char stamp[32];
        _snprintf(stamp, sizeof(stamp) - 1, "%lu", (unsigned long)GetTickCount()); stamp[sizeof(stamp) - 1] = 0;
        std::string ignored;
        ftp.Upload(JoinRemote(dir, "pc_link.flag"), stamp, ignored);
        bool found = false;
        if (!ftp.Download(JoinRemote(dir, "X360Remap_status.json"), r.status, found, r.err)) return;
        if (!found) r.status.clear();
        r.ok = true;
        return;
    }
    if (job.type == JOB_COVER) {
        char prefix[16], file[32];
        _snprintf(prefix, sizeof(prefix) - 1, "%08X", (unsigned)job.titleId); prefix[sizeof(prefix) - 1] = 0;
        _snprintf(file, sizeof(file) - 1, "GC%08X.asset", (unsigned)job.titleId); file[sizeof(file) - 1] = 0;
        for (size_t i = 0; i < job.gameDataDirs.size(); i++) {
            if (_strnicmp(job.gameDataDirs[i].c_str(), prefix, 8) != 0) continue;
            bool found = false;
            if (!ftp.Download(job.auroraRoot + "/Data/GameData/" + job.gameDataDirs[i] + "/" + file, r.cover, found, r.err)) return;
            if (found) break;
        }
        r.ok = true;
        return;
    }
}

// --- devices plugged into the console now (X360Remap_status.json) ---------------
struct PluggedDev { char kind; uint16_t vid, pid; };
std::vector<PluggedDev> g_plugged;
bool     g_statusPending = false;
double   g_statusAskedAt = -100.0, g_statusFreshAt = -100.0;
uint32_t g_statusUptime = 0;

void ApplyStatus(const std::string& json) {
    rapidjson::Document d;
    if (json.empty() || d.Parse(json.c_str()).HasParseError() || !d.IsObject()) return;   // mid-write: next read
    const uint32_t up = d.HasMember("uptimeMs") && d["uptimeMs"].IsUint() ? d["uptimeMs"].GetUint() : 0;
    // A file left from an old session has an uptime that never moves: only a change proves
    // the plugin is writing it for us now.
    const bool alive = up != 0 && up != g_statusUptime;
    g_statusUptime = up;
    if (!alive) return;
    g_plugged.clear();
    if (d.HasMember("devices") && d["devices"].IsArray()) {
        const rapidjson::Value& a = d["devices"];
        for (rapidjson::SizeType i = 0; i < a.Size(); i++) {
            if (!a[i].IsObject() || !a[i].HasMember("vid") || !a[i]["vid"].IsString() || !a[i].HasMember("pid") ||
                !a[i]["pid"].IsString() || !a[i].HasMember("kind") || !a[i]["kind"].IsString()) continue;
            const std::string kind = a[i]["kind"].GetString();
            PluggedDev p;
            p.kind = kind == "keyboard" ? 'k' : kind == "mouse" ? 'm' : kind == "controller" ? 'c' : 0;
            p.vid = (uint16_t)strtoul(a[i]["vid"].GetString(), nullptr, 16);
            p.pid = (uint16_t)strtoul(a[i]["pid"].GetString(), nullptr, 16);
            if (p.kind) g_plugged.push_back(p);
        }
    }
    g_statusFreshAt = plat::Seconds();
}

bool StatusFresh() { return g_state == LINK_CONNECTED && plat::Seconds() - g_statusFreshAt < 10.0; }

void WorkerMain() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_wake.wait(lock, [] { return g_quit || !g_jobs.empty(); });
            if (g_quit) return;
            job = g_jobs.front();
            g_jobs.pop_front();
        }
        JobResult r;
        RunJob(job, r);
        std::lock_guard<std::mutex> lock(g_mutex);
        g_results.push_back(r);
    }
}

void Enqueue(const Job& job) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_jobs.push_back(job);
        if (job.type != JOB_COVER && job.type != JOB_STATUS) g_running++;
    }
    g_wake.notify_one();
}

bool HaveAddress() {
    if (plat::Settings().consoleIp[0]) return true;
    plat::Toast("%s", L4("Indique d'abord l'adresse de la console", "Enter the console address first",
                         "Indica primero la direcci\xC3\xB3n de la consola", "Informe primeiro o endere\xC3\xA7o do console"));
    return false;
}

// ============================================================================
// Results (main thread)
// ============================================================================

void ReadPluginVersion() {
    std::string log;
    g_pluginVersion.clear();
    g_dashboardBuild = 0;
    if (!plat::ReadWholeFile(plat::WorkFile(kLogFiles[0]), log)) return;
    const char* marker = "X360Remap plugin v";
    const std::string::size_type p = log.find(marker);
    if (p == std::string::npos) return;
    std::string::size_type b = p + strlen(marker), e = b;
    while (e < log.size() && log[e] != ' ' && log[e] != '\r' && log[e] != '\n') e++;
    g_pluginVersion = log.substr(b, e - b);

    // Console Studio: "App: main() started - X360Remap Studio 1.3-pre11"
    g_studioVersion.clear();
    std::string studioLog;
    if (plat::ReadWholeFile(plat::WorkFile(kLogFiles[2]), studioLog)) {
        const char* sm = "X360Remap Studio ";
        const std::string::size_type sp = studioLog.find(sm);
        if (sp != std::string::npos) {
            std::string::size_type sb = sp + strlen(sm), se = sb;
            while (se < studioLog.size() && studioLog[se] != ' ' && studioLog[se] != '\r' && studioLog[se] != '\n') se++;
            g_studioVersion = studioLog.substr(sb, se - sb);
        }
    }

    // Same line: "... starting, dashboard build 17559 ---"
    g_dashboardBuild = 0;
    const std::string::size_type eol = log.find('\n', p);
    const char* dash = "dashboard build ";
    const std::string::size_type d = log.find(dash, p);
    if (d != std::string::npos && (eol == std::string::npos || d < eol)) g_dashboardBuild = atoi(log.c_str() + d + strlen(dash));
}

bool IsPristineConfig(const std::string& json) {
    for (size_t i = 0; i < json.size(); i++) {
        const char c = json[i];
        if (c != '[' && c != ']' && c != ' ' && c != '\r' && c != '\n' && c != '\t') return false;
    }
    return true;
}

// A writable file came from the console: take it, or ask when the working copy
// holds changes that were never sent.
void ApplyPulledWritable(const FileData& f) {
    if (!f.found) return;
    const std::string workPath = plat::WorkFile(f.name.c_str()), basePath = plat::BaseFile(f.name.c_str());
    std::string local, base;
    const bool hasLocal = plat::ReadWholeFile(workPath, local);
    const bool hasBase = plat::ReadWholeFile(basePath, base);

    bool localEdited;
    if (!hasLocal) localEdited = false;
    else if (hasBase) localEdited = (local != base);
    else localEdited = !(f.name == kWritableFiles[0] ? IsPristineConfig(local) : local.empty());

    if (!localEdited || local == f.data) {
        if (!hasLocal || local != f.data) {
            plat::WriteWholeFile(workPath, f.data);
            ModelWorkingCopyChanged();
        }
        plat::WriteWholeFile(basePath, f.data);
        return;
    }
    if (hasBase && f.data == base) return;   // console unchanged, local edits wait for a push

    Conflict c;
    c.name = f.name;
    c.consoleData = f.data;
    c.duringPush = false;
    g_conflicts.push_back(c);
}

void ApplyResult(const JobResult& r) {
    if (r.type == JOB_CHECK_UPDATE) {
        g_update.checked = true;
        g_update.err = r.ok ? std::string() : r.err;
        if (r.ok) {
            g_update.release = r.release;
            g_update.available = r.release.id != plat::Settings().seenRelease;
        }
        return;
    }
    if (r.type == JOB_DOWNLOAD_UPDATE) {
        g_update.downloading = false;
        if (!r.ok) {
            plat::Toast("GitHub: %s", r.err.c_str());
            return;
        }
        g_update.pluginXex = r.pluginXex;
        g_update.studioXex = r.studioXex;
        g_update.downloadedName = r.release.name;
        return;
    }
    if (r.type == JOB_SCAN) {
        g_scanRunning = false;
        g_scanResults = r.found;
        // Also after "Find my console" while not connected (HB 2026-11-17: the scan listed
        // .102 but the saved .101 stayed, because the automatic search had already run once
        // for .101 while the console was still off).
        if (g_refindPending || g_state != LINK_CONNECTED) {
            // The console did not answer at its saved address: if the network shows exactly
            // one console elsewhere, it simply got another address from the router.
            g_refindPending = false;
            WinSettings& s = plat::Settings();
            const std::string moved = PickMovedConsole(r.found, s.consoleIp);
            if (!moved.empty()) {
                const std::string before = s.consoleIp;
                _snprintf(s.consoleIp, sizeof(s.consoleIp) - 1, "%s", moved.c_str());
                s.consoleIp[sizeof(s.consoleIp) - 1] = 0;
                plat::SaveSettings();
                plat::Log("Console moved from %s to %s", before.c_str(), moved.c_str());
                plat::Toast(L4("Console retrouv\xC3\xA9" "e \xC3\xA0 %s (son adresse a chang\xC3\xA9)", "Console found at %s (its address changed)",
                               "Consola encontrada en %s (su direcci\xC3\xB3n cambi\xC3\xB3)", "Console encontrado em %s (o endere\xC3\xA7o mudou)"),
                            moved.c_str());
                Pull();
            } else {
                g_refindFor.clear();   // nothing found (console still off?): search again on the next failure
            }
        }
        return;
    }
    if (r.type == JOB_STATUS) {
        g_statusPending = false;
        if (r.ok) ApplyStatus(r.status);
        return;   // a missed read changes nothing: the next one comes in 3 s
    }
    if (r.type == JOB_COVER) {
        if (!r.cover.empty()) {
            char name[40];
            _snprintf(name, sizeof(name) - 1, "covers\\%08X.asset", (unsigned)r.titleId); name[sizeof(name) - 1] = 0;
            plat::WriteWholeFile(plat::CacheFile(name), r.cover);
            g_auroraGeneration++;
        }
        return;
    }

    if (r.installChecked) {
        g_install.checked = true;
        g_install.check = r.install;
        if (r.type == JOB_CHECK_INSTALL) g_install.logActive = r.logActive;
    }
    if (!r.backupDir.empty() && r.backupCount > 0) g_install.lastBackupDir = r.backupDir;
    if (r.ok && r.type == JOB_BACKUP)
        plat::Toast("%d %s", r.backupCount, L4("fichier(s) de réglages copiés sur le PC", "settings file(s) copied to the PC",
                                               "archivo(s) de ajustes copiados al PC", "arquivo(s) de configuração copiados para o PC"));
    if (r.ok && r.type == JOB_INSTALL)
        plat::Toast("%s", L4("Installé et vérifié - redémarre la console", "Installed and verified - restart the console",
                             "Instalado y verificado - reinicia la consola", "Instalado e verificado - reinicie o console"));
    if (r.type == JOB_REBOOT) {
        g_install.reboot = r.reboot;
        g_install.pluginVersionAfter = r.pluginVersionAfter;
    }
    if (r.ok && r.auroraScriptWritten) g_install.auroraScriptWritten = true;
    if (r.ok && r.launchIniWritten)
        plat::Toast("%s", L4("launch.ini corrigé (copie launch.ini.bak) - redémarre la console",
                             "launch.ini fixed (copy in launch.ini.bak) - restart the console",
                             "launch.ini corregido (copia launch.ini.bak) - reinicia la consola",
                             "launch.ini corrigido (cópia launch.ini.bak) - reinicie o console"));

    if (!r.ok) {
        g_state = LINK_ERROR;
        g_lastError = r.err;
        plat::Toast("%s: %s", L4("Console", "Console", "Consola", "Console"), r.err.c_str());
        // One search of the network per address that stopped answering (HB's console went
        // from .102 to .101 to .100 in two weeks).
        if (r.type == JOB_PULL && r.connectFailed && !g_scanRunning && g_refindFor != plat::Settings().consoleIp) {
            g_refindFor = plat::Settings().consoleIp;
            g_refindPending = true;
            Scan();
        }
    } else {
        g_state = LINK_CONNECTED;
        g_lastError.clear();
        g_lastExchange = plat::Seconds();
    }
    if (r.haveNames) g_remoteNames = r.remoteNames;

    // Read-only files: the working copy simply follows the console.
    for (size_t i = 0; i < r.files.size(); i++) {
        const FileData& f = r.files[i];
        bool writable = false;
        for (int k = 0; k < kWritableCount; k++) writable = writable || f.name == kWritableFiles[k];
        if (writable) { ApplyPulledWritable(f); continue; }
        if (f.found) plat::WriteWholeFile(plat::WorkFile(f.name.c_str()), f.data);
    }
    if (!r.files.empty()) { ReadPluginVersion(); ModelWorkingCopyChanged(); }

    if (r.auroraFound) {
        g_auroraFound = true;
        g_auroraRoot = r.auroraRoot;
        g_gameDataDirs = r.gameDataDirs;
        plat::WriteWholeFile(plat::CacheFile("content.db"), r.contentDb);
        g_auroraGeneration++;
    }

    if (r.type == JOB_PULL && r.ok) {
        plat::Toast("%s", g_conflicts.empty()
            ? L4("R\xC3\xA9glages de la console charg\xC3\xA9s", "Console settings loaded",
                 "Ajustes de la consola cargados", "Configura\xC3\xA7\xC3\xB5""es do console carregadas")
            : L4("Modifi\xC3\xA9 des deux c\xC3\xB4t\xC3\xA9s : \xC3\xA0 toi de choisir", "Changed on both sides: your choice",
                 "Cambiado en ambos lados: t\xC3\xBA eliges", "Alterado dos dois lados: voc\xC3\xAA escolhe"));
    }

    if (r.type == JOB_PUSH) {
        int sent = 0, same = 0;
        for (size_t i = 0; i < r.pushed.size(); i++) {
            const PushedFile& p = r.pushed[i];
            if (p.out.result == SAFE_OK || p.out.result == SAFE_UNCHANGED) {
                plat::WriteWholeFile(plat::BaseFile(p.name.c_str()), p.local);
                if (p.out.result == SAFE_OK) sent++; else same++;
            } else if (p.out.result == SAFE_CONFLICT) {
                Conflict c;
                c.name = p.name;
                c.consoleData = p.out.consoleData;
                c.duringPush = true;
                g_conflicts.push_back(c);
            }
        }
        if (r.ok && g_conflicts.empty()) {
            if (sent == 0)
                plat::Toast("%s", L4("La console a d\xC3\xA9j\xC3\xA0 ces r\xC3\xA9glages", "The console already has these settings",
                                     "La consola ya tiene estos ajustes", "O console j\xC3\xA1 tem estas configura\xC3\xA7\xC3\xB5""es"));
            else if (r.reloadSeen == 1)
                plat::Toast("%s", L4("Envoy\xC3\xA9 - le plugin a recharg\xC3\xA9 les r\xC3\xA9glages", "Sent - the plugin reloaded the settings",
                                     "Enviado - el plugin recarg\xC3\xB3 los ajustes", "Enviado - o plugin recarregou as configura\xC3\xA7\xC3\xB5""es"));
            else
                plat::Toast("%s", L4("Envoy\xC3\xA9 \xC3\xA0 la console et v\xC3\xA9rifi\xC3\xA9", "Sent to the console and verified",
                                     "Enviado a la consola y verificado", "Enviado ao console e verificado"));
        }
        plat::Log("push: %d sent, %d unchanged, %d conflict(s), reload=%d, err=%s",
                  sent, same, (int)g_conflicts.size(), r.reloadSeen, r.err.c_str());
    }
}

void AskStatus() {
    if (g_state != LINK_CONNECTED || g_statusPending || plat::Seconds() - g_statusAskedAt < 3.0) return;
    g_statusPending = true;
    g_statusAskedAt = plat::Seconds();
    Job job;
    job.type = JOB_STATUS;
    job.cfg = plat::Settings();
    Enqueue(job);
}

void RefreshDirty() {
    static double last = -1.0;
    const double now = plat::Seconds();
    if (now - last < 0.5) return;
    last = now;
    bool dirty = false;
    for (int i = 0; i < kWritableCount; i++) {
        std::string local, base;
        if (!plat::ReadWholeFile(plat::WorkFile(kWritableFiles[i]), local)) continue;
        // The in-game display file exists on the PC only once it was saved there: with no
        // copy from the console yet, that save is still to send.
        if (!plat::ReadWholeFile(plat::BaseFile(kWritableFiles[i]), base)) { dirty = dirty || i == 2; continue; }
        dirty = dirty || (local != base);
    }
    g_dirty = dirty;
}

}  // namespace

// ============================================================================
// Public
// ============================================================================

void Start() {
    if (g_thread.joinable()) return;
    g_quit = false;
    g_thread = std::thread(WorkerMain);
}

void Stop() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_quit = true;
    }
    g_wake.notify_all();
    // A job in the middle of a network wait ends on its own time-out; the
    // process does not wait for it.
    if (g_thread.joinable()) g_thread.detach();
}

void Update() {
    for (;;) {
        JobResult r;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_results.empty()) break;
            r = g_results.front();
            g_results.pop_front();
            if (r.type != JOB_COVER && r.type != JOB_STATUS && g_running > 0) g_running--;
        }
        ApplyResult(r);
    }
    RefreshDirty();
    AskStatus();
}

int ConsolePlugged(char kind, uint16_t vid, uint16_t pid) {
    if (!StatusFresh()) return -1;
    for (size_t i = 0; i < g_plugged.size(); i++)
        if (g_plugged[i].kind == kind && g_plugged[i].vid == vid && g_plugged[i].pid == pid) return 1;
    return 0;
}

int ConsolePluggedPads(uint16_t* vid, uint16_t* pid, int max) {
    if (!StatusFresh()) return 0;
    int n = 0;
    for (size_t i = 0; i < g_plugged.size() && n < max; i++)
        if (g_plugged[i].kind == 'c') { vid[n] = g_plugged[i].vid; pid[n] = g_plugged[i].pid; n++; }
    return n;
}

bool ConsoleHasPlugged(char kind) {
    if (!StatusFresh()) return false;
    for (size_t i = 0; i < g_plugged.size(); i++)
        if (g_plugged[i].kind == kind) return true;
    return false;
}

void Pull() {
    if (Busy() || !HaveAddress()) return;
    Job job;
    job.type = JOB_PULL;
    job.cfg = plat::Settings();
    job.wantAurora = !g_auroraTried;
    g_auroraTried = true;
    Enqueue(job);
}

void Push() {
    if (Busy() || !HaveAddress()) return;
    if (!IsStudioDir(plat::Settings().remoteDir)) {
        plat::Toast("%s", L4("Le dossier distant doit \xC3\xAAtre X360RemapStudio", "The remote folder must be X360RemapStudio",
                             "La carpeta remota debe ser X360RemapStudio", "A pasta remota deve ser X360RemapStudio"));
        return;
    }
    Job job;
    job.type = JOB_PUSH;
    job.cfg = plat::Settings();
    for (int i = 0; i < kWritableCount; i++) {
        PushFile f;
        f.name = kWritableFiles[i];
        f.force = false;
        if (!plat::ReadWholeFile(plat::WorkFile(kWritableFiles[i]), f.local)) continue;
        f.hasBase = plat::ReadWholeFile(plat::BaseFile(kWritableFiles[i]), f.base);
        // The camera and in-game display files only go when they hold something or the console had one.
        if (i >= 1 && f.local.empty() && !f.hasBase) continue;

        // Never send a file the plugin would reject: X360Remap.json is checked
        // with the plugin's own parser, camera lines with CamParseLine.
        bool valid = true;
        if (i == 0) {
            valid = ModelValidateConfigJson(f.local);
        } else if (i == 1) {
            size_t start = 0;
            while (start < f.local.size() && valid) {
                size_t end = f.local.find('\n', start);
                if (end == std::string::npos) end = f.local.size();
                std::string line = f.local.substr(start, end - start);
                start = end + 1;
                while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == ' ')) line.erase(line.size() - 1);
                if (line.empty() || line[0] == '#') continue;
                LearnedCamera cam;
                valid = CamParseLine(line.c_str(), &cam);
            }
        }
        if (!valid) {
            plat::Toast("%s: %s", f.name.c_str(), L4("fichier invalide, rien n'est envoy\xC3\xA9", "invalid file, nothing was sent",
                                                     "archivo no v\xC3\xA1lido, no se envi\xC3\xB3 nada", "arquivo inv\xC3\xA1lido, nada foi enviado"));
            return;
        }
        job.files.push_back(f);
    }
    if (job.files.empty()) return;
    Enqueue(job);
}

void RefreshLogs() {
    if (Busy() || !HaveAddress()) return;
    Job job;
    job.type = JOB_LOGS;
    job.cfg = plat::Settings();
    Enqueue(job);
}

void SetSwitch(const std::string& fileName, bool on) {
    if (Busy() || !HaveAddress()) return;
    Job job;
    job.type = JOB_SWITCH;
    job.cfg = plat::Settings();
    job.switchName = fileName;
    job.switchOn = on;
    Enqueue(job);
}

void Scan() {
    if (g_scanRunning) return;
    g_scanRunning = true;
    g_scanResults.clear();
    Job job;
    job.type = JOB_SCAN;
    Enqueue(job);
}

void RequestCover(uint32_t titleId) {
    if (!g_auroraFound || g_state != LINK_CONNECTED) return;
    for (size_t i = 0; i < g_coversAsked.size(); i++)
        if (g_coversAsked[i] == titleId) return;
    g_coversAsked.push_back(titleId);
    Job job;
    job.type = JOB_COVER;
    job.cfg = plat::Settings();
    job.titleId = titleId;
    job.auroraRoot = g_auroraRoot;
    job.gameDataDirs = g_gameDataDirs;
    Enqueue(job);
}

LinkState State() { return g_state; }

bool Busy() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_running > 0;
}

const std::string& LastError() { return g_lastError; }
const std::string& PluginVersion() { return g_pluginVersion; }
int DashboardBuild() { return g_dashboardBuild; }
const std::string& StudioVersion() { return g_studioVersion; }
bool Dirty() { return g_dirty; }
double LastExchangeAt() { return g_lastExchange; }

const Conflict* PendingConflict() { return g_conflicts.empty() ? nullptr : &g_conflicts[0]; }

void ResolveConflict(int choice) {
    if (g_conflicts.empty()) return;
    const Conflict c = g_conflicts[0];
    g_conflicts.erase(g_conflicts.begin());
    if (choice == 0) {
        // Keep mine: the console content becomes the known base, so the next
        // push replaces it without asking again.
        plat::WriteWholeFile(plat::BaseFile(c.name.c_str()), c.consoleData);
        if (c.duringPush) g_repushAfterConflicts = true;
    } else if (choice == 1) {
        plat::WriteWholeFile(plat::WorkFile(c.name.c_str()), c.consoleData);
        plat::WriteWholeFile(plat::BaseFile(c.name.c_str()), c.consoleData);
        ModelWorkingCopyChanged();
    }
    if (g_conflicts.empty() && g_repushAfterConflicts) {
        g_repushAfterConflicts = false;
        Push();
    }
}

const std::vector<std::string>& RemoteNames() { return g_remoteNames; }
bool ScanRunning() { return g_scanRunning; }
const std::vector<FoundFtp>& ScanResults() { return g_scanResults; }
bool AuroraFound() { return g_auroraFound; }

const InstallState& Install() { return g_install; }

static void EnqueueInstallJob(Job& job) {
    if (Busy() || !HaveAddress()) return;
    job.cfg = plat::Settings();
    if (!IsStudioDir(job.cfg.remoteDir)) {
        plat::Toast("%s", L4("Le dossier distant doit être X360RemapStudio", "The remote folder must be X360RemapStudio",
                             "La carpeta remota debe ser X360RemapStudio", "A pasta remota deve ser X360RemapStudio"));
        return;
    }
    Enqueue(job);
}

const UpdateState& UpdateInfo() { return g_update; }

void CheckForUpdate() {
    Job job;
    job.type = JOB_CHECK_UPDATE;
    job.cfg = plat::Settings();
    Enqueue(job);
}

void DownloadUpdate() {
    if (!g_update.checked || g_update.release.id == 0 || g_update.downloading) return;
    g_update.downloading = true;
    Job job;
    job.type = JOB_DOWNLOAD_UPDATE;
    job.cfg = plat::Settings();
    job.release = g_update.release;
    Enqueue(job);
}

bool TakeDownloadedUpdate(std::string& pluginXex, std::string& studioXex, std::string& name) {
    if (g_update.pluginXex.empty()) return false;
    pluginXex.swap(g_update.pluginXex);
    studioXex.swap(g_update.studioXex);
    name = g_update.downloadedName;
    g_update.pluginXex.clear();
    g_update.studioXex.clear();
    return true;
}

void MarkUpdateSeen() {
    if (g_update.release.id == 0) return;
    plat::Settings().seenRelease = g_update.release.id;
    plat::SaveSettings();
    g_update.available = false;
}

void InstallAuroraSetupScript() { Job job; job.type = JOB_AURORA_SCRIPT; EnqueueInstallJob(job); }

void RebootAndWait() {
    g_install.reboot = REBOOT_WAITING;
    Job job;
    job.type = JOB_REBOOT;
    EnqueueInstallJob(job);
}

void CheckInstallation() { Job job; job.type = JOB_CHECK_INSTALL; EnqueueInstallJob(job); }
void BackupSettings() { Job job; job.type = JOB_BACKUP; EnqueueInstallJob(job); }

void InstallXex(const std::string& pluginXex, const std::string& studioXex) {
    Job job;
    job.type = JOB_INSTALL;
    job.pluginXex = pluginXex;
    job.studioXex = studioXex;
    EnqueueInstallJob(job);
}

void ApplyLaunchIniFix() {
    if (!g_install.checked || !g_install.check.fixOffered) return;
    Job job;
    job.type = JOB_FIX_INI;
    job.shown = g_install.check;   // exactly the change the user was shown
    EnqueueInstallJob(job);
}
uint32_t AuroraGeneration() { return g_auroraGeneration; }

}  // namespace consync
