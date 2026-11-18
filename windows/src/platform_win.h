#pragma once
// What the console Studio gets from the Xbox APIs, for the Windows build:
// folders, files, clock, notifications (shown as toasts in the window),
// settings, file dialogs, clipboard, and a tiny zip writer for "Send logs".
//
// All paths are UTF-8 (the exe manifest sets the UTF-8 code page), so they can
// be handed as-is to the shared code (mapping.cpp, config_writer.cpp), which
// opens files with std::ifstream / fopen.
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

struct WinSettings {
    char consoleIp[64];
    int  ftpPort;
    char ftpUser[32];
    char ftpPassword[32];
    char remoteDir[128];    // FTP folder of HDD:\X360RemapStudio\ ("/Hdd1/X360RemapStudio/")
    int  language;          // Lang (application/i18n.h)
    bool autoConnect;       // connect at start when an IP is remembered
    bool checkUpdates;      // look for a new release on GitHub at start (off by default)
    bool updatePreviews;    // previews count as new releases
    long long seenRelease;  // GitHub id of the last release shown to the user
};

namespace plat {

// Creates the folders, loads the settings. The data folder is, in this order:
// dataDirOverride (--data-dir), "data" next to the exe when a file named
// portable.txt sits there, else %LOCALAPPDATA%\X360RemapWindows.
void Init(const char* dataDirOverride);

// Data folder, ends with a backslash.
const std::string& DataDir();
// Working copy of HDD:\X360RemapStudio\ : what the pages edit.
std::string WorkFile(const char* name);
// Content of the console file at our last download / upload (conflict check).
std::string BaseFile(const char* name);
// Aurora data fetched for the Game profiles page (content.db, covers).
std::string CacheFile(const char* name);

bool ReadWholeFile(const std::string& path, std::string& out);
bool WriteWholeFile(const std::string& path, const std::string& data);
bool FileExists(const std::string& path);
void RemoveFile(const std::string& path);
// Changes whenever the file changes (size and write time), 0 if absent.
uint64_t FileStamp(const std::string& path);

double   Seconds();
void     Log(const char* fmt, ...);        // DataDir()\X360RemapWindows.log
void     Toast(const char* fmt, ...);      // short message shown in the window
bool     CurrentToast(std::string& text, float& alpha);

WinSettings& Settings();
void SaveSettings();

// filter: "Name\0*.ext\0\0" (wide). false if the user cancelled.
bool OpenFileDialog(void* hwnd, const wchar_t* filter, std::string& pathOut);
bool SaveFileDialog(void* hwnd, const wchar_t* filter, const wchar_t* defaultName,
                    const wchar_t* defaultExt, std::string& pathOut);
void OpenUrl(const char* url);
void OpenFolder(const std::string& path);
// Unzips (stored / deflate) into destDir; false on a damaged zip or an entry
// leaving destDir (zip_win.cpp). FindFileBelow: `name` anywhere below dir,
// empty if missing.
bool ExtractZip(const std::string& zipPath, const std::string& destDir);
std::string FindFileBelow(const std::string& dir, const char* name);

// Keyboards ('K'), mice ('M') and game controllers ('G') plugged into this PC,
// with the USB VID:PID and the name the device gives. Devices without a USB
// id (laptop keyboard, touchpad, remote desktop) are left out.
struct PcDevice {
    uint16_t    vid, pid;
    char        kind;
    std::string name;
};
std::vector<PcDevice> ListPcDevices();
// Parses "\\?\HID#VID_046D&PID_C52B&MI_00#..." (USB) or the Bluetooth
// "...VID&0002046d_PID&b023..." form. Exposed for the tests.
bool ParseVidPid(const std::string& path, uint16_t& vid, uint16_t& pid);
void SetClipboard(const char* utf8);
std::string GetClipboard();
// The shared imconfig.h disables ImGui's own Win32 clipboard (Xbox build), so
// the callbacks are installed by hand. Call after ImGui::CreateContext().
void InstallImGuiClipboard();

// Zip without compression (store): enough for a few text logs, no dependency.
bool WriteZip(const std::string& path,
              const std::vector<std::pair<std::string, std::string> >& files);

}  // namespace plat
