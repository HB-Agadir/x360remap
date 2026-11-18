#pragma once
// Link to the console: a small FTP client for Aurora's FTP server (the same
// server NetISO Manager uses), and the "safe write" used for every file sent.
//
// No UI and no global state here: sync.cpp drives it from a worker thread, and
// windows/tests/test_console_link.cpp drives it against a fake server.
#include <stdint.h>
#include <string>
#include <vector>

class FtpClient {
public:
    FtpClient();
    ~FtpClient();

    bool Connect(const char* host, int port, const char* user, const char* password, std::string& err);
    void Close();
    bool IsOpen() const;
    const std::string& Banner() const { return banner_; }

    // File and folder names of a directory (names only, no path).
    bool ListNames(const std::string& dir, std::vector<std::string>& names, std::string& err);

    // found = false (and true returned) when the file does not exist.
    bool Download(const std::string& path, std::string& data, bool& found, std::string& err);
    bool Upload(const std::string& path, const std::string& data, std::string& err);
    bool Rename(const std::string& from, const std::string& to, std::string& err);
    bool Delete(const std::string& path, std::string& err);
    // Creates the folder; true if it exists afterwards (already there counts).
    bool MakeDir(const std::string& path, std::string& err);

private:
    FtpClient(const FtpClient&);
    FtpClient& operator=(const FtpClient&);

    int  ReadReply(std::string& text);
    int  Command(const std::string& line, std::string& text);
    bool OpenData(uintptr_t& dataSocket, std::string& err);
    bool Fail(std::string& err, const char* what, const std::string& reply);

    uintptr_t   ctrl_;
    std::string host_;
    std::string banner_;
    std::string pending_;   // bytes received after the last complete reply
};

// --- Safe write ---------------------------------------------------------------
// One file of HDD:\X360RemapStudio\, sent so that the console file is either
// the old one or the new one, never a partial one:
//   1. download the console file; if it changed since our last download
//      (base) and is not already what we want to send -> SAFE_CONFLICT;
//   2. upload "<name>.new", download it again, compare byte for byte;
//   3. keep the previous content as "<name>.bak";
//   4. rename "<name>.new" over "<name>", download and compare once more.
// A cut at any point before step 4 leaves the console file untouched.
enum SafeWriteResult {
    SAFE_OK = 0,        // sent and verified
    SAFE_UNCHANGED,     // the console already has exactly this content
    SAFE_CONFLICT,      // changed on the console since our last download (see consoleData)
    SAFE_ERROR          // see err; the console file was not replaced
};

struct SafeWriteOutcome {
    SafeWriteResult result;
    std::string     consoleData;    // console content read in step 1
    bool            consoleFound;
    std::string     err;
    SafeWriteOutcome() : result(SAFE_ERROR), consoleFound(false) {}
};

// remoteDir: "/Hdd1/X360RemapStudio/". name: plain file name.
// hasBase / base: content of the console file at our last download or upload.
// force: skip the conflict check ("keep mine").
SafeWriteOutcome SafeWriteFile(FtpClient& ftp, const std::string& remoteDir, const std::string& name,
                               const std::string& local, bool hasBase, const std::string& base, bool force);

// Same steps for any path, WITHOUT the folder check: only for callers that
// check the path themselves (SafeWriteFile, WriteLaunchIni in install_win.h).
SafeWriteOutcome SafeWriteAt(FtpClient& ftp, const std::string& path,
                             const std::string& local, bool hasBase, const std::string& base, bool force);

// Never write outside HDD:\X360RemapStudio\ : the folder must be named
// X360RemapStudio and the file name must be a plain name.
bool IsStudioDir(const std::string& remoteDir);
bool IsPlainFileName(const std::string& name);
std::string JoinRemote(const std::string& dir, const std::string& name);

// --- Reboot -------------------------------------------------------------------
// Cold reboot through the debug monitor (xbdm.xex, TCP 730): "magicboot cold",
// the request XeCLI sends for "rgh reboot". false when xbdm does not answer
// (not loaded: the user restarts the console by hand).
bool XbdmColdReboot(const char* host, std::string& err, int port = 730);

// One text connection to the debug monitor. Replies: "2xx- text", and "202-"
// is followed by lines up to a single ".". Never xdevkit.dll / JRPC.dll.
class XbdmConn {
public:
    XbdmConn();
    ~XbdmConn();
    bool Connect(const char* host, int port, int timeoutMs, std::string& err);
    void Close();
    bool IsOpen() const;
    // false only when the link failed or timed out. status: the first line.
    bool Command(const std::string& line, std::string& status, std::vector<std::string>& lines, int timeoutMs = 2500);
    // Next line of the stream. false + timedOut: nothing yet, the link is fine.
    bool ReadLine(std::string& line, int timeoutMs, bool& timedOut);
    // false when xbdm refuses or the bytes are unreadable.
    bool GetMem(uint32_t addr, uint32_t length, std::string& out);

private:
    XbdmConn(const XbdmConn&);
    XbdmConn& operator=(const XbdmConn&);
    uintptr_t   sock_;
    std::string pending_;
};

// --- "Find my console" ----------------------------------------------------------
struct FoundFtp {
    std::string ip;
    std::string banner;
    bool        xbdm;      // the debug monitor (port 730) answers there too: an Xbox 360
    FoundFtp() : xbdm(false) {}
};
// Tries port 21 on every address of the local /24 networks. Blocking (about
// two seconds); call from a worker thread.
void ScanLocalFtp(std::vector<FoundFtp>& out);
// The console moved (its address comes from the router and changes): the one address of
// the scan that is a console and is not the saved one, or "" when there is none or several.
std::string PickMovedConsole(const std::vector<FoundFtp>& found, const std::string& savedIp);
