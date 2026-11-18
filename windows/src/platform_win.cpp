#include "platform_win.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cctype>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

#include "imgui.h"

namespace {

std::string  g_dataDir;
WinSettings  g_settings;
std::string  g_toastText;
double       g_toastAt = -100.0;
const double kToastSeconds = 3.5;

std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string Narrow(const wchar_t* w) {
    if (!w || !w[0]) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    s.resize(strlen(s.c_str()));
    return s;
}

void MakeDir(const std::string& path) { CreateDirectoryW(Widen(path).c_str(), nullptr); }

void CopyText(char* dst, size_t dstSize, const std::string& src) {
    _snprintf(dst, dstSize - 1, "%s", src.c_str());
    dst[dstSize - 1] = '\0';
}

void DefaultSettings() {
    memset(&g_settings, 0, sizeof(g_settings));
    g_settings.ftpPort = 21;
    // Aurora's FTP server defaults (the ones NetISO Manager uses), editable.
    CopyText(g_settings.ftpUser, sizeof(g_settings.ftpUser), "xboxftp");
    CopyText(g_settings.ftpPassword, sizeof(g_settings.ftpPassword), "xboxftp");
    CopyText(g_settings.remoteDir, sizeof(g_settings.remoteDir), "/Hdd1/X360RemapStudio/");
    // English until the user picks a flag (HB 2026-10-24; Lang in i18n.h:
    // 0 French, 1 English, 2 Portuguese, 3 Spanish).
    g_settings.language = 1;
    g_settings.autoConnect = true;
    g_settings.checkUpdates = false;
    g_settings.updatePreviews = false;
    g_settings.seenRelease = 0;
}

void LoadSettings() {
    DefaultSettings();
    std::string text;
    if (!plat::ReadWholeFile(g_dataDir + "settings.ini", text)) return;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        const std::string::size_type eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        if (key == "console_ip")        CopyText(g_settings.consoleIp, sizeof(g_settings.consoleIp), val);
        else if (key == "ftp_port")     g_settings.ftpPort = atoi(val.c_str());
        else if (key == "ftp_user")     CopyText(g_settings.ftpUser, sizeof(g_settings.ftpUser), val);
        else if (key == "ftp_password") CopyText(g_settings.ftpPassword, sizeof(g_settings.ftpPassword), val);
        else if (key == "remote_dir")   CopyText(g_settings.remoteDir, sizeof(g_settings.remoteDir), val);
        else if (key == "language")     g_settings.language = atoi(val.c_str());
        else if (key == "auto_connect") g_settings.autoConnect = atoi(val.c_str()) != 0;
        else if (key == "check_updates") g_settings.checkUpdates = atoi(val.c_str()) != 0;
        else if (key == "update_previews") g_settings.updatePreviews = atoi(val.c_str()) != 0;
        else if (key == "seen_release") g_settings.seenRelease = _atoi64(val.c_str());
    }
    if (g_settings.ftpPort <= 0 || g_settings.ftpPort > 65535) g_settings.ftpPort = 21;
    if (g_settings.language < 0 || g_settings.language > 3) g_settings.language = 1;
}

// --- zip (store) ---------------------------------------------------------------
uint32_t Crc32(const std::string& data) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < data.size(); i++) crc = table[(crc ^ (unsigned char)data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}
void Put16(std::string& o, uint16_t v) { o.push_back((char)(v & 0xFF)); o.push_back((char)(v >> 8)); }
void Put32(std::string& o, uint32_t v) { Put16(o, (uint16_t)(v & 0xFFFF)); Put16(o, (uint16_t)(v >> 16)); }

}  // namespace

namespace plat {

void Init(const char* dataDirOverride) {
    wchar_t base[MAX_PATH] = L"";
    wchar_t exe[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::string exeDir = Narrow(exe);
    exeDir.erase(exeDir.find_last_of('\\') + 1);

    if (dataDirOverride && dataDirOverride[0]) {
        g_dataDir = dataDirOverride;
    } else if (FileExists(exeDir + "portable.txt")) {
        // Portable copy (USB stick, zip): everything stays next to the exe.
        g_dataDir = exeDir + "data";
    } else {
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, base)))
            GetTempPathW(MAX_PATH, base);
        g_dataDir = Narrow(base);
        if (!g_dataDir.empty() && g_dataDir[g_dataDir.size() - 1] != '\\') g_dataDir += '\\';
        g_dataDir += "X360RemapWindows";
    }
    if (g_dataDir[g_dataDir.size() - 1] != '\\') g_dataDir += '\\';
    MakeDir(g_dataDir);
    MakeDir(g_dataDir + "work");
    MakeDir(g_dataDir + "base");
    MakeDir(g_dataDir + "cache");
    MakeDir(g_dataDir + "cache\\covers");
    MakeDir(g_dataDir + "cache\\install");
    MakeDir(g_dataDir + "cache\\downloads");
    LoadSettings();
}

const std::string& DataDir() { return g_dataDir; }
std::string WorkFile(const char* name)  { return g_dataDir + "work\\" + name; }
std::string BaseFile(const char* name)  { return g_dataDir + "base\\" + name; }
std::string CacheFile(const char* name) { return g_dataDir + "cache\\" + name; }

bool ReadWholeFile(const std::string& path, std::string& out) {
    std::ifstream f(Widen(path).c_str(), std::ios::binary);
    if (!f.is_open()) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

// Written next to the target then moved over it: a crash in the middle leaves
// the previous file intact.
bool WriteWholeFile(const std::string& path, const std::string& data) {
    const std::wstring target = Widen(path), part = target + L".part";
    {
        std::ofstream f(part.c_str(), std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return false;
        f.write(data.data(), (std::streamsize)data.size());
        if (!f.good()) return false;
    }
    return MoveFileExW(part.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

bool FileExists(const std::string& path) {
    return GetFileAttributesW(Widen(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

void RemoveFile(const std::string& path) { DeleteFileW(Widen(path).c_str()); }

uint64_t FileStamp(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(Widen(path).c_str(), GetFileExInfoStandard, &d)) return 0;
    const uint64_t t = ((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
    return (t * 1000003ull) ^ (((uint64_t)d.nFileSizeLow << 1) | 1ull);
}

double Seconds() {
    static LARGE_INTEGER freq, start;
    static bool ready = false;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!ready) { QueryPerformanceFrequency(&freq); start = now; ready = true; }
    return (double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart;
}

void Log(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    buf[sizeof(buf) - 1] = '\0';
    va_end(args);
    static bool truncated = false;
    std::ofstream f(Widen(g_dataDir + "X360RemapWindows.log").c_str(),
                    std::ios::binary | (truncated ? std::ios::app : std::ios::trunc));
    truncated = true;
    if (!f.is_open()) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char stamp[32];
    _snprintf(stamp, sizeof(stamp) - 1, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    stamp[sizeof(stamp) - 1] = '\0';
    f << stamp << buf << "\r\n";
}

void Toast(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    buf[sizeof(buf) - 1] = '\0';
    va_end(args);
    g_toastText = buf;
    g_toastAt = Seconds();
    Log("toast: %s", buf);
}

bool CurrentToast(std::string& text, float& alpha) {
    const double age = Seconds() - g_toastAt;
    if (age < 0.0 || age > kToastSeconds || g_toastText.empty()) return false;
    text = g_toastText;
    const double left = kToastSeconds - age;
    alpha = left < 0.6 ? (float)(left / 0.6) : 1.0f;
    return true;
}

WinSettings& Settings() { return g_settings; }

void SaveSettings() {
    std::ostringstream o;
    o << "console_ip=" << g_settings.consoleIp << "\r\n"
      << "ftp_port=" << g_settings.ftpPort << "\r\n"
      << "ftp_user=" << g_settings.ftpUser << "\r\n"
      << "ftp_password=" << g_settings.ftpPassword << "\r\n"
      << "remote_dir=" << g_settings.remoteDir << "\r\n"
      << "language=" << g_settings.language << "\r\n"
      << "auto_connect=" << (g_settings.autoConnect ? 1 : 0) << "\r\n"
      << "check_updates=" << (g_settings.checkUpdates ? 1 : 0) << "\r\n"
      << "update_previews=" << (g_settings.updatePreviews ? 1 : 0) << "\r\n"
      << "seen_release=" << g_settings.seenRelease << "\r\n";
    WriteWholeFile(g_dataDir + "settings.ini", o.str());
}

bool OpenFileDialog(void* hwnd, const wchar_t* filter, std::string& pathOut) {
    wchar_t file[1024] = L"";
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (HWND)hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = 1024;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return false;
    pathOut = Narrow(file);
    return true;
}

bool SaveFileDialog(void* hwnd, const wchar_t* filter, const wchar_t* defaultName,
                    const wchar_t* defaultExt, std::string& pathOut) {
    wchar_t file[1024] = L"";
    if (defaultName) wcsncpy(file, defaultName, 1023);
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (HWND)hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = 1024;
    ofn.lpstrDefExt = defaultExt;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return false;
    pathOut = Narrow(file);
    return true;
}

void OpenUrl(const char* url) {
    if (!url || !url[0]) return;
    std::string u = url;
    if (u.compare(0, 4, "http") != 0) u = "https://" + u;
    ShellExecuteW(nullptr, L"open", Widen(u).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void OpenFolder(const std::string& path) {
    ShellExecuteW(nullptr, L"open", Widen(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string FindFileBelow(const std::string& dir, const char* name) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(Widen(dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return std::string();
    std::string found;
    do {
        const std::string n = Narrow(fd.cFileName);
        if (n == "." || n == "..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) found = FindFileBelow(dir + "\\" + n, name);
        else if (_stricmp(n.c_str(), name) == 0) found = dir + "\\" + n;
    } while (found.empty() && FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

void SetClipboard(const char* utf8) {
    if (!OpenClipboard(nullptr)) return;
    const std::wstring w = Widen(utf8 ? utf8 : "");
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (h) {
        memcpy(GlobalLock(h), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(h);
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
    }
    CloseClipboard();
}

std::string GetClipboard() {
    std::string out;
    if (!OpenClipboard(nullptr)) return out;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* w = (const wchar_t*)GlobalLock(h);
        if (w) out = Narrow(w);
        GlobalUnlock(h);
    }
    CloseClipboard();
    return out;
}

static const char* ImGuiGetClipboard(void*) {
    static std::string s;
    s = GetClipboard();
    return s.c_str();
}
static void ImGuiSetClipboard(void*, const char* text) { SetClipboard(text); }

void InstallImGuiClipboard() {
    ImGuiIO& io = ImGui::GetIO();
    io.GetClipboardTextFn = ImGuiGetClipboard;
    io.SetClipboardTextFn = ImGuiSetClipboard;
    io.ClipboardUserData = nullptr;
}

bool WriteZip(const std::string& path,
              const std::vector<std::pair<std::string, std::string> >& files) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    const uint16_t dosTime = (uint16_t)((st.wHour << 11) | (st.wMinute << 5) | (st.wSecond / 2));
    const uint16_t dosDate = (uint16_t)(((st.wYear - 1980) << 9) | (st.wMonth << 5) | st.wDay);

    std::string out, central;
    for (size_t i = 0; i < files.size(); i++) {
        const std::string& name = files[i].first;
        const std::string& data = files[i].second;
        const uint32_t crc = Crc32(data), offset = (uint32_t)out.size();
        Put32(out, 0x04034b50); Put16(out, 20); Put16(out, 0); Put16(out, 0);
        Put16(out, dosTime); Put16(out, dosDate); Put32(out, crc);
        Put32(out, (uint32_t)data.size()); Put32(out, (uint32_t)data.size());
        Put16(out, (uint16_t)name.size()); Put16(out, 0);
        out += name;
        out += data;

        Put32(central, 0x02014b50); Put16(central, 20); Put16(central, 20); Put16(central, 0); Put16(central, 0);
        Put16(central, dosTime); Put16(central, dosDate); Put32(central, crc);
        Put32(central, (uint32_t)data.size()); Put32(central, (uint32_t)data.size());
        Put16(central, (uint16_t)name.size()); Put16(central, 0); Put16(central, 0);
        Put16(central, 0); Put16(central, 0); Put32(central, 0); Put32(central, offset);
        central += name;
    }
    const uint32_t centralOffset = (uint32_t)out.size();
    out += central;
    Put32(out, 0x06054b50); Put16(out, 0); Put16(out, 0);
    Put16(out, (uint16_t)files.size()); Put16(out, (uint16_t)files.size());
    Put32(out, (uint32_t)central.size()); Put32(out, centralOffset); Put16(out, 0);
    return WriteWholeFile(path, out);
}

}  // namespace plat
