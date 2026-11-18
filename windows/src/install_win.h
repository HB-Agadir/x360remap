#pragma once
// Install & repair (VISION.md, section 5c), over Aurora's FTP:
//   - check DashLaunch's launch.ini with the console Studio's own rules
//     (application/plugin_diag.h);
//   - back up the console's settings before anything is replaced;
//   - put X360Remap.xex / X360RemapStudio.xex in X360RemapStudio\;
//   - fix the plugin line of launch.ini, only after the user saw the exact
//     change and said yes: launch.ini.bak first, one line only.
// No UI and no global state: sync.cpp runs it, tests/test_windows.cpp tests it.
#include <string>
#include <utility>
#include <vector>
#include "console_link.h"
#include "../../application/plugin_diag.h"

// The line the plugin should have, once installed by this app.
// HB's layout (2026-10-26): the plugin at the root of the drive, the Studio
// in X360RemapStudio\.
extern const char* const kPluginDashPath;   // "Hdd:\X360Remap.xex"
// "/Hdd1/X360RemapStudio/" -> "/Hdd1/X360Remap.xex"
std::string PluginFtpPath(const std::string& studioDir);

struct InstallCheck {
    bool             iniFound;
    std::string      iniFtpPath;          // "/Hdd1/launch.ini"
    std::string      iniText;
    PluginDiagResult diag;
    bool             pluginPresent; // X360Remap.xex at the root of the drive
    bool             studioAppPresent;    // X360RemapStudio.xex there too
    // Proposed launch.ini change, shown to the user before anything is written.
    bool             fixOffered;
    std::string      fixOldLine;          // empty when a line is added
    std::string      fixNewLine;
    std::string      fixedIni;
    bool             slotsFull;           // plugin1..plugin5 all used by other plugins
    // Aurora (its FTP root "/Game" is Aurora's own folder): is X360RemapStudio
    // one of its scan paths, so the Studio shows up in its applications?
    bool             auroraChecked;       // settings.db read
    bool             auroraHasStudio;
    InstallCheck() : iniFound(false), pluginPresent(false), studioAppPresent(false), fixOffered(false), slotsFull(false),
                     auroraChecked(false), auroraHasStudio(false) {
        diag.diag = DIAG_NO_INI;
    }
};

// studioDir: "/Hdd1/X360RemapStudio/". false = network error (err).
bool CheckInstall(FtpClient& ftp, const std::string& studioDir, InstallCheck& out, std::string& err);

// The launch.ini text with the plugin line set to newValue: the existing
// X360Remap line is changed, or an empty pluginN slot of [Plugins] is filled,
// or a new pluginN line is added. Every other byte stays as it was.
// false if nothing needs to change.
bool ProposeLaunchIniFix(const std::string& ini, const std::string& currentValue, const std::string& newValue,
                         std::string& oldLine, std::string& newLine, std::string& fixedIni);

// Writes the confirmed change: only to the launch.ini found by CheckInstall,
// only if it still holds `expected` (else nothing is written), previous
// content kept as launch.ini.bak.
SafeWriteOutcome WriteLaunchIni(FtpClient& ftp, const std::string& iniFtpPath, const std::string& expected,
                                const std::string& fixedIni);

// Aurora utility script "X360Remap Setup": run once from Aurora's utility
// scripts, it adds \X360RemapStudio (hard drive) to Aurora's scan paths as
// "Applications", then offers to restart Aurora. Same mechanism NetISO Manager
// uses (Aurora 0.7b.2, script permission "sql"); Aurora keeps its databases
// locked while it runs, so only Aurora itself may change them.
extern const char* const kAuroraScriptPath;   // "/Game/User/Scripts/Utility/X360Remap Setup/Main.lua"
std::string AuroraSetupScript();
bool InstallAuroraScript(FtpClient& ftp, std::string& err);

// Every settings file of the Studio folder (not the .xex, not .new / .bak).
bool DownloadStudioSettings(FtpClient& ftp, const std::string& studioDir,
                            std::vector<std::pair<std::string, std::string> >& files, std::string& err);
