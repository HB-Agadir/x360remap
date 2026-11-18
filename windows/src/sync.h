#pragma once
// Pull -> local working copy -> push.
//
// The pages always edit the working copy on the PC (plat::WorkFile). This
// module brings the console files into it (Pull), sends the changed ones back
// with the safe write of console_link.h (Push), and remembers what the console
// had at the last exchange (plat::BaseFile) to detect a change made on the
// console in between.
//
// All network work runs on one worker thread; results are applied on the main
// thread by Update(), so the pages never see a half-written file.
#include <stdint.h>
#include <string>
#include <vector>
#include "console_link.h"
#include "install_win.h"
#include "update_win.h"

namespace consync {

enum LinkState { LINK_OFFLINE = 0, LINK_CONNECTED, LINK_ERROR };

struct Conflict {
    std::string name;          // file of X360RemapStudio
    std::string consoleData;   // what the console has
    bool        duringPush;
};

void Start();
void Stop();
void Update();   // once per frame, main thread

void Pull();
void Push();
void RefreshLogs();
void SetSwitch(const std::string& fileName, bool on);
void Scan();
void RequestCover(uint32_t titleId);   // Aurora cover of a game, into the cache

LinkState State();
// Devices plugged into the console NOW, from X360Remap_status.json (read every 3 s
// while connected, CONTRACT.md). kind 'k' keyboard, 'm' mouse, 'c' controller.
// -1 = not known (no fresh status), 0 = not plugged, 1 = plugged.
int  ConsolePlugged(char kind, uint16_t vid, uint16_t pid);
// At least one device of this kind plugged now (false when not known).
bool ConsoleHasPlugged(char kind);
// Controllers plugged into the console now, in the plugin's order. Returns the count (<= max).
int  ConsolePluggedPads(uint16_t* vid, uint16_t* pid, int max);
bool Busy();
const std::string& LastError();
const std::string& PluginVersion();    // "1.3-pre9", empty if unknown
int DashboardBuild();                  // 17559, 0 if unknown (same log line)
const std::string& StudioVersion();    // console Studio, first line of its log; empty if unknown
bool Dirty();                          // working copy differs from the console's
double LastExchangeAt();               // plat::Seconds() of the last success, < 0 if none

const Conflict* PendingConflict();
void ResolveConflict(int choice);      // 0 = keep mine, 1 = take the console's, 2 = cancel

// Files of the console folder at the last listing (on/off switches).
const std::vector<std::string>& RemoteNames();
bool ScanRunning();
const std::vector<FoundFtp>& ScanResults();
bool AuroraFound();

enum RebootState { REBOOT_NONE = 0, REBOOT_WAITING, REBOOT_PLUGIN_RUNNING, REBOOT_NO_PLUGIN, REBOOT_NOT_BACK, REBOOT_NO_XBDM };

// Install & repair (VISION.md 5c). Results arrive in Install().
struct InstallState {
    bool         checked;
    InstallCheck check;
    int          logActive;       // -1 unknown, 0 the plugin log did not change in 3 s, 1 it changed
    std::string  lastBackupDir;   // last backup of the console settings on the PC
    bool         auroraScriptWritten;
    int          reboot;          // RebootState
    std::string  pluginVersionAfter;
    InstallState() : checked(false), logActive(-1), auroraScriptWritten(false), reboot(0) {}
};
const InstallState& Install();
void CheckInstallation();
void BackupSettings();
// Backs the settings up to the PC first, then sends both files to X360RemapStudio\.
void InstallXex(const std::string& pluginXex, const std::string& studioXex);
// Writes the change shown by the last check (launch.ini.bak kept). Call only
// after the user saw Install().check.fixOldLine / fixNewLine and said yes.
void ApplyLaunchIniFix();
// Puts the "X360Remap Setup" utility script in Aurora (the user runs it there).
void InstallAuroraSetupScript();
// Cold reboot over xbdm, then waits (up to 4 min) for the plugin's new log.
void RebootAndWait();

// New release on GitHub (only when the user turned it on, or asked).
struct UpdateState {
    bool        checked, available, downloading;
    ReleaseInfo release;              // newest release found
    std::string err;
    std::string pluginXex, studioXex, downloadedName;   // downloaded, waiting for the yes
    UpdateState() : checked(false), available(false), downloading(false) {}
};
const UpdateState& UpdateInfo();
void CheckForUpdate();
void DownloadUpdate();
// The downloaded files, once (then the install dialog asks for the yes).
bool TakeDownloadedUpdate(std::string& pluginXex, std::string& studioXex, std::string& name);
// "Do not tell me again about this release."
void MarkUpdateSeen();
// Increments each time Aurora data (names, a cover) arrived in the cache.
uint32_t AuroraGeneration();

// Names of the two files this tool writes, and of the ones it only reads.
extern const char* const kWritableFiles[3];
extern const char* const kLogFiles[4];

}  // namespace consync
