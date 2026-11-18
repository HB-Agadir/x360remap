#include "console_link.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace {

const int kTimeoutMs = 10000;
const uintptr_t kNoSocket = (uintptr_t)INVALID_SOCKET;

void EnsureWinsock() {
    static bool started = false;
    if (started) return;
    WSADATA wsa;
    started = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
}

void SetTimeouts(SOCKET s, int ms) {
    DWORD t = (DWORD)ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&t, sizeof(t));
}

// Connect with a timeout (a console that is off must not block for 21 s).
SOCKET ConnectTo(const char* host, int port, int timeoutMs) {
    EnsureWinsock();
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        addrinfo hints, *res = nullptr;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host, nullptr, &hints, &res) != 0 || !res) return INVALID_SOCKET;
        addr.sin_addr = ((sockaddr_in*)res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    connect(s, (sockaddr*)&addr, sizeof(addr));
    fd_set wr, ex;
    FD_ZERO(&wr); FD_SET(s, &wr);
    FD_ZERO(&ex); FD_SET(s, &ex);
    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    const int r = select(0, nullptr, &wr, &ex, &tv);
    if (r <= 0 || !FD_ISSET(s, &wr)) { closesocket(s); return INVALID_SOCKET; }
    nonBlocking = 0;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    SetTimeouts(s, kTimeoutMs);
    return s;
}

bool SendAll(SOCKET s, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        const size_t chunk = (len - sent > 16384) ? 16384 : (len - sent);
        const int n = send(s, data + sent, (int)chunk, 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

std::string TrimReply(const std::string& s) {
    std::string r = s;
    while (!r.empty() && (r[r.size() - 1] == '\r' || r[r.size() - 1] == '\n')) r.erase(r.size() - 1);
    return r;
}

std::string BaseName(const std::string& s) {
    const std::string::size_type p = s.find_last_of("/\\");
    return p == std::string::npos ? s : s.substr(p + 1);
}

}  // namespace

// ============================================================================
// FtpClient
// ============================================================================

FtpClient::FtpClient() : ctrl_(kNoSocket) {}
FtpClient::~FtpClient() { Close(); }

bool FtpClient::IsOpen() const { return ctrl_ != kNoSocket; }

void FtpClient::Close() {
    if (ctrl_ != kNoSocket) {
        const char quit[] = "QUIT\r\n";
        send((SOCKET)ctrl_, quit, 6, 0);
        closesocket((SOCKET)ctrl_);
        ctrl_ = kNoSocket;
    }
    pending_.clear();
}

bool FtpClient::Fail(std::string& err, const char* what, const std::string& reply) {
    err = what;
    if (!reply.empty()) { err += ": "; err += TrimReply(reply); }
    return false;
}

// One complete reply ("226 done", or a multi-line "211-... 211 end").
// Returns its code, or -1 when the connection is lost.
int FtpClient::ReadReply(std::string& text) {
    text.clear();
    int  code = -1;
    bool multi = false;
    for (;;) {
        std::string::size_type eol;
        while ((eol = pending_.find('\n')) != std::string::npos) {
            const std::string line = pending_.substr(0, eol + 1);
            pending_.erase(0, eol + 1);
            text += line;
            const bool numbered = line.size() >= 4 && isdigit((unsigned char)line[0]) &&
                                  isdigit((unsigned char)line[1]) && isdigit((unsigned char)line[2]);
            if (code < 0) {
                if (!numbered) continue;
                code = atoi(line.substr(0, 3).c_str());
                multi = (line[3] == '-');
                if (!multi) return code;
            } else if (numbered && atoi(line.substr(0, 3).c_str()) == code && line[3] == ' ') {
                return code;
            }
        }
        char buf[2048];
        const int n = recv((SOCKET)ctrl_, buf, sizeof(buf), 0);
        if (n <= 0) return -1;
        pending_.append(buf, (size_t)n);
        if (pending_.size() > 1024 * 1024) return -1;
    }
}

int FtpClient::Command(const std::string& line, std::string& text) {
    text.clear();
    if (ctrl_ == kNoSocket) return -1;
    const std::string out = line + "\r\n";
    if (!SendAll((SOCKET)ctrl_, out.data(), out.size())) return -1;
    return ReadReply(text);
}

bool FtpClient::Connect(const char* host, int port, const char* user, const char* password, std::string& err) {
    Close();
    host_ = host ? host : "";
    if (host_.empty()) { err = "no console address"; return false; }
    const SOCKET s = ConnectTo(host_.c_str(), port, 4000);
    if (s == INVALID_SOCKET) { err = "no answer from " + host_; return false; }
    ctrl_ = (uintptr_t)s;

    std::string text;
    int code = ReadReply(text);
    banner_ = TrimReply(text);
    if (code / 100 != 2) { Close(); return Fail(err, "not an FTP server", text); }

    code = Command(std::string("USER ") + (user ? user : ""), text);
    if (code / 100 == 3) code = Command(std::string("PASS ") + (password ? password : ""), text);
    if (code / 100 != 2) { Close(); return Fail(err, "login refused", text); }

    code = Command("TYPE I", text);
    if (code / 100 != 2) { Close(); return Fail(err, "binary mode refused", text); }
    return true;
}

// Passive mode: the server tells which port to connect to for the data.
bool FtpClient::OpenData(uintptr_t& dataSocket, std::string& err) {
    dataSocket = kNoSocket;
    std::string text;
    const int code = Command("PASV", text);
    if (code != 227) return Fail(err, "passive mode refused", text);
    const std::string::size_type open = text.find('(');
    int h1, h2, h3, h4, p1, p2;
    if (open == std::string::npos ||
        sscanf(text.c_str() + open, "(%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6)
        return Fail(err, "passive mode answer not understood", text);
    // The address in the answer is ignored on purpose: a console behind the
    // same router always answers with its own address, and some servers
    // report 0.0.0.0.
    const SOCKET s = ConnectTo(host_.c_str(), p1 * 256 + p2, 4000);
    if (s == INVALID_SOCKET) { err = "data connection failed"; return false; }
    dataSocket = (uintptr_t)s;
    return true;
}

// Aurora's FtpDll (checked on a console, 2026-10-24) ignores the folder given
// to NLST / LIST and lists the current directory, NLST included in the long
// "-rwxrwxrwx 1 root root 42 Oct 18 08:50 name" format: so CWD first, list
// without argument, and read either format.
bool FtpClient::ListNames(const std::string& dir, std::vector<std::string>& names, std::string& err) {
    names.clear();
    std::string text;
    const int cwd = Command("CWD " + dir, text);
    if (cwd == 550 || cwd == 450) return true;   // no such folder: empty
    const std::string arg = (cwd / 100 == 2) ? std::string() : " " + dir;
    for (int attempt = 0; attempt < 2; attempt++) {
        const bool nlst = (attempt == 0);
        uintptr_t ds;
        if (!OpenData(ds, err)) return false;
        int code = Command(std::string(nlst ? "NLST" : "LIST") + arg, text);
        if (code / 100 != 1) {
            closesocket((SOCKET)ds);
            if (nlst && code >= 500) continue;          // server without NLST: try LIST
            if (code == 550 || code == 450) return true; // empty folder
            return Fail(err, "list refused", text);
        }
        std::string data;
        char buf[8192];
        int n;
        while ((n = recv((SOCKET)ds, buf, sizeof(buf), 0)) > 0) data.append(buf, (size_t)n);
        closesocket((SOCKET)ds);
        code = ReadReply(text);
        if (code / 100 != 2) return Fail(err, "list interrupted", text);

        size_t start = 0;
        while (start < data.size()) {
            size_t end = data.find('\n', start);
            if (end == std::string::npos) end = data.size();
            std::string line = data.substr(start, end - start);
            start = end + 1;
            while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == ' ')) line.erase(line.size() - 1);
            if (line.empty()) continue;
            const bool longFormat = !nlst || (line.size() > 10 && strchr("-dl", line[0]) &&
                                              strspn(line.c_str() + 1, "rwxsStT-") >= 9);
            if (longFormat) {
                // "drwxr-xr-x 1 root root 0 Jan 01 2010 name with spaces": the
                // name starts after the 8th column.
                size_t pos = 0;
                int fields = 0;
                while (fields < 8 && pos < line.size()) {
                    while (pos < line.size() && line[pos] != ' ') pos++;
                    while (pos < line.size() && line[pos] == ' ') pos++;
                    fields++;
                }
                if (fields < 8 || pos >= line.size()) continue;
                line = line.substr(pos);
            }
            line = BaseName(line);
            if (line.empty() || line == "." || line == "..") continue;
            names.push_back(line);
        }
        return true;
    }
    return true;
}

bool FtpClient::Download(const std::string& path, std::string& data, bool& found, std::string& err) {
    data.clear();
    found = false;
    // SIZE before RETR: after a refused RETR, Aurora's FtpDll keeps our data
    // connection queued on its single passive port and sends the NEXT transfer
    // into it, so every following download hangs. A missing file must never
    // reach RETR.
    std::string text;
    int code = Command("SIZE " + path, text);
    if (code == 550) return true;   // no such file
    const bool sized = (code == 213);
    const unsigned long long size = sized ? strtoull(text.c_str() + 4, nullptr, 10) : 0;
    uintptr_t ds;
    if (!OpenData(ds, err)) return false;
    code = Command("RETR " + path, text);
    if (code / 100 != 1) {
        closesocket((SOCKET)ds);
        if (code == 550) return true;   // no such file
        return Fail(err, "download refused", text);
    }
    char buf[16384];
    int n;
    while ((n = recv((SOCKET)ds, buf, sizeof(buf), 0)) > 0) data.append(buf, (size_t)n);
    const bool cut = (n < 0);
    closesocket((SOCKET)ds);
    code = ReadReply(text);
    if (cut || code / 100 != 2 || (sized && size != data.size())) {
        data.clear();
        return Fail(err, "download interrupted", text);
    }
    found = true;
    return true;
}

bool FtpClient::Upload(const std::string& path, const std::string& data, std::string& err) {
    uintptr_t ds;
    if (!OpenData(ds, err)) return false;
    std::string text;
    int code = Command("STOR " + path, text);
    if (code / 100 != 1) {
        closesocket((SOCKET)ds);
        return Fail(err, "upload refused", text);
    }
    const bool sent = SendAll((SOCKET)ds, data.data(), data.size());
    shutdown((SOCKET)ds, SD_SEND);
    closesocket((SOCKET)ds);
    code = ReadReply(text);
    if (!sent || code / 100 != 2) return Fail(err, "upload interrupted", text);
    return true;
}

bool FtpClient::Rename(const std::string& from, const std::string& to, std::string& err) {
    std::string text;
    int code = Command("RNFR " + from, text);
    if (code / 100 != 3) return Fail(err, "rename refused", text);
    code = Command("RNTO " + to, text);
    if (code / 100 != 2) return Fail(err, "rename refused", text);
    return true;
}

bool FtpClient::MakeDir(const std::string& path, std::string& err) {
    std::string text;
    const int code = Command("MKD " + path, text);
    if (code / 100 == 2) return true;
    // "already exists" answers differ between servers: the parent's listing settles it.
    std::string p = path;
    while (!p.empty() && p[p.size() - 1] == '/') p.erase(p.size() - 1);
    const std::string::size_type slash = p.find_last_of('/');
    std::vector<std::string> names;
    std::string listErr;
    if (code > 0 && slash != std::string::npos && ListNames(p.substr(0, slash + 1), names, listErr))
        for (size_t i = 0; i < names.size(); i++)
            if (_stricmp(names[i].c_str(), p.substr(slash + 1).c_str()) == 0) return true;
    return Fail(err, "folder creation refused", text);
}

bool FtpClient::Delete(const std::string& path, std::string& err) {
    std::string text;
    const int code = Command("DELE " + path, text);
    if (code / 100 != 2) return Fail(err, "delete refused", text);
    return true;
}

// ============================================================================
// Safe write
// ============================================================================

bool IsPlainFileName(const std::string& name) {
    if (name.empty() || name.size() > 80 || name[0] == '.') return false;
    for (size_t i = 0; i < name.size(); i++) {
        const unsigned char c = (unsigned char)name[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) return false;
    }
    return name.find("..") == std::string::npos;
}

bool IsStudioDir(const std::string& remoteDir) {
    std::string d = remoteDir;
    while (!d.empty() && d[d.size() - 1] == '/') d.erase(d.size() - 1);
    if (d.find("..") != std::string::npos) return false;
    const std::string::size_type p = d.find_last_of('/');
    if (p == std::string::npos || p == 0) return false;   // must be inside a drive ("/Hdd1/...")
    return _stricmp(d.substr(p + 1).c_str(), "X360RemapStudio") == 0;
}

std::string JoinRemote(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir[dir.size() - 1] == '/') return dir + name;
    return dir + "/" + name;
}

SafeWriteOutcome SafeWriteFile(FtpClient& ftp, const std::string& remoteDir, const std::string& name,
                               const std::string& local, bool hasBase, const std::string& base, bool force) {
    if (!IsStudioDir(remoteDir) || !IsPlainFileName(name)) {
        SafeWriteOutcome out;
        out.err = "refused: only files of the X360RemapStudio folder are written";
        return out;
    }
    return SafeWriteAt(ftp, JoinRemote(remoteDir, name), local, hasBase, base, force);
}

// No folder check here: the callers check the path (SafeWriteFile, WriteLaunchIni).
SafeWriteOutcome SafeWriteAt(FtpClient& ftp, const std::string& path,
                             const std::string& local, bool hasBase, const std::string& base, bool force) {
    SafeWriteOutcome out;
    const std::string name = BaseName(path);
    const std::string pathNew = path + ".new", pathBak = path + ".bak";

    // 1. what the console has now
    if (!ftp.Download(path, out.consoleData, out.consoleFound, out.err)) return out;
    if (out.consoleFound && out.consoleData == local) { out.result = SAFE_UNCHANGED; return out; }
    if (!force && out.consoleFound && (!hasBase || out.consoleData != base)) {
        out.result = SAFE_CONFLICT;
        return out;
    }

    // 2. upload next to the original, read it back
    if (!ftp.Upload(pathNew, local, out.err)) return out;
    std::string check;
    bool found = false;
    std::string ignored;
    if (!ftp.Download(pathNew, check, found, out.err)) { ftp.Delete(pathNew, ignored); return out; }
    if (!found || check != local) {
        ftp.Delete(pathNew, ignored);
        out.err = "the copy read back from the console differs from what was sent";
        return out;
    }

    // 3. previous content kept as .bak (a copy: the original is not touched)
    if (out.consoleFound && !ftp.Upload(pathBak, out.consoleData, out.err)) {
        ftp.Delete(pathNew, ignored);
        return out;
    }

    // 4. replace. Some servers refuse to rename over an existing file: the
    //    original is then removed first (the .bak and the verified .new both
    //    exist at that moment).
    std::string renameErr;
    if (!ftp.Rename(pathNew, path, renameErr)) {
        if (!out.consoleFound || !ftp.Delete(path, ignored) || !ftp.Rename(pathNew, path, renameErr)) {
            out.err = "could not replace the console file (" + renameErr + ") - previous content is in " + name + ".bak";
            return out;
        }
    }
    if (!ftp.Download(path, check, found, out.err)) return out;
    if (!found || check != local) {
        out.err = "the console file differs after the replacement - previous content is in " + name + ".bak";
        return out;
    }
    out.result = SAFE_OK;
    return out;
}

// ============================================================================
// Find my console
// ============================================================================

bool XbdmColdReboot(const char* host, std::string& err, int port) {
    const SOCKET s = ConnectTo(host, port, 3000);
    if (s == INVALID_SOCKET) { err = "the debug monitor (xbdm) does not answer"; return false; }
    char buf[256];
    const int n = recv(s, buf, sizeof(buf) - 1, 0);   // "201- connected"
    if (n < 3 || buf[0] != '2') { closesocket(s); err = "unexpected answer from xbdm"; return false; }
    const char cmd[] = "magicboot cold\r\n";
    const bool sent = SendAll(s, cmd, sizeof(cmd) - 1);
    recv(s, buf, sizeof(buf) - 1, 0);   // the console may drop the link at once
    closesocket(s);
    if (!sent) err = "the reboot request could not be sent";
    return sent;
}

XbdmConn::XbdmConn() : sock_(kNoSocket) {}
XbdmConn::~XbdmConn() { Close(); }
bool XbdmConn::IsOpen() const { return sock_ != kNoSocket; }

void XbdmConn::Close() {
    if (sock_ != kNoSocket) closesocket((SOCKET)sock_);
    sock_ = kNoSocket;
    pending_.clear();
}

bool XbdmConn::Connect(const char* host, int port, int timeoutMs, std::string& err) {
    Close();
    const SOCKET s = ConnectTo(host, port, timeoutMs);
    if (s == INVALID_SOCKET) { err = "the debug monitor (xbdm) does not answer"; return false; }
    sock_ = (uintptr_t)s;
    std::string hello;
    bool timedOut = false;
    if (!ReadLine(hello, timeoutMs, timedOut) || hello.empty() || hello[0] != '2') {
        Close();
        err = "unexpected answer from xbdm";
        return false;
    }
    return true;
}

bool XbdmConn::ReadLine(std::string& line, int timeoutMs, bool& timedOut) {
    timedOut = false;
    if (sock_ == kNoSocket) return false;
    for (;;) {
        const size_t end = pending_.find("\r\n");
        if (end != std::string::npos) {
            line.assign(pending_, 0, end);
            pending_.erase(0, end + 2);
            return true;
        }
        fd_set set;
        FD_ZERO(&set);
        FD_SET((SOCKET)sock_, &set);
        timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        const int ready = select(0, &set, nullptr, nullptr, &tv);
        if (ready == 0) { timedOut = true; return false; }
        char buf[4096];
        const int n = ready < 0 ? -1 : recv((SOCKET)sock_, buf, sizeof(buf), 0);
        if (n <= 0) { Close(); return false; }
        pending_.append(buf, n);
    }
}

bool XbdmConn::Command(const std::string& line, std::string& status, std::vector<std::string>& lines, int timeoutMs) {
    lines.clear();
    status.clear();
    if (sock_ == kNoSocket) return false;
    const std::string out = line + "\r\n";
    bool timedOut = false;
    if (!SendAll((SOCKET)sock_, out.data(), out.size()) || !ReadLine(status, timeoutMs, timedOut)) { Close(); return false; }
    if (status.compare(0, 3, "202") != 0) return true;
    for (;;) {
        std::string l;
        if (!ReadLine(l, timeoutMs, timedOut)) { Close(); return false; }
        if (l == ".") return true;
        lines.push_back(l);
    }
}

bool XbdmConn::GetMem(uint32_t addr, uint32_t length, std::string& out) {
    out.clear();
    char cmd[80], hex[3] = { 0, 0, 0 };
    _snprintf(cmd, sizeof(cmd) - 1, "getmem addr=0x%08x length=0x%x", addr, length);
    cmd[sizeof(cmd) - 1] = 0;
    std::string status;
    std::vector<std::string> lines;
    if (!Command(cmd, status, lines)) return false;
    for (size_t i = 0; i < lines.size(); i++)
        for (size_t k = 0; k + 1 < lines[i].size(); k += 2) {
            if (!isxdigit((unsigned char)lines[i][k]) || !isxdigit((unsigned char)lines[i][k + 1])) return false;   // "??": unreadable
            hex[0] = lines[i][k]; hex[1] = lines[i][k + 1];
            out.push_back((char)strtoul(hex, nullptr, 16));
        }
    return out.size() == length;
}

void ScanLocalFtp(std::vector<FoundFtp>& out) {
    out.clear();
    EnsureWinsock();

    // /24 prefixes of the private IPv4 addresses of this PC
    std::vector<uint32_t> prefixes;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf(size);
    ULONG r = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                   nullptr, (IP_ADAPTER_ADDRESSES*)&buf[0], &size);
    if (r == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        r = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                 nullptr, (IP_ADAPTER_ADDRESSES*)&buf[0], &size);
    }
    if (r != NO_ERROR) return;
    for (IP_ADAPTER_ADDRESSES* a = (IP_ADAPTER_ADDRESSES*)&buf[0]; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
            const uint32_t ip = ntohl(((sockaddr_in*)u->Address.lpSockaddr)->sin_addr.s_addr);
            const bool isPrivate = (ip >> 24) == 10 || (ip >> 20) == 0xAC1 || (ip >> 16) == 0xC0A8;
            if (!isPrivate) continue;
            const uint32_t prefix = ip & 0xFFFFFF00u;
            bool have = false;
            for (size_t i = 0; i < prefixes.size(); i++) have = have || prefixes[i] == prefix;
            if (!have && prefixes.size() < 4) prefixes.push_back(prefix);
        }
    }

    for (size_t p = 0; p < prefixes.size(); p++) {
        SOCKET socks[254];
        for (int i = 0; i < 254; i++) {
            socks[i] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (socks[i] == INVALID_SOCKET) continue;
            u_long nonBlocking = 1;
            ioctlsocket(socks[i], FIONBIO, &nonBlocking);
            sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(21);
            addr.sin_addr.s_addr = htonl(prefixes[p] | (uint32_t)(i + 1));
            connect(socks[i], (sockaddr*)&addr, sizeof(addr));
        }
        Sleep(900);   // a LAN answers in a few milliseconds
        for (int i = 0; i < 254; i++) {
            if (socks[i] == INVALID_SOCKET) continue;
            fd_set wr;
            FD_ZERO(&wr); FD_SET(socks[i], &wr);
            timeval tv = { 0, 0 };
            if (select(0, nullptr, &wr, nullptr, &tv) == 1) {
                u_long nonBlocking = 0;
                ioctlsocket(socks[i], FIONBIO, &nonBlocking);
                SetTimeouts(socks[i], 800);
                char banner[256];
                const int n = recv(socks[i], banner, sizeof(banner) - 1, 0);
                if (n >= 3 && banner[0] == '2') {
                    banner[n] = '\0';
                    FoundFtp f;
                    const uint32_t ip = prefixes[p] | (uint32_t)(i + 1);
                    char text[32];
                    _snprintf(text, sizeof(text) - 1, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
                    text[sizeof(text) - 1] = '\0';
                    f.ip = text;
                    f.banner = TrimReply(banner);
                    out.push_back(f);
                }
            }
            closesocket(socks[i]);
        }
    }
    // An FTP server alone may be a NAS or a PC: the debug monitor makes it a console.
    for (size_t i = 0; i < out.size(); i++) {
        XbdmConn probe;
        std::string err;
        out[i].xbdm = probe.Connect(out[i].ip.c_str(), 730, 600, err);
    }
}

std::string PickMovedConsole(const std::vector<FoundFtp>& found, const std::string& savedIp) {
    std::string pick;
    for (size_t i = 0; i < found.size(); i++) {
        // "FtpDll" is the greeting of Aurora's FTP server
        const bool console = found[i].xbdm || found[i].banner.find("FtpDll") != std::string::npos;
        if (!console || found[i].ip == savedIp) continue;
        if (!pick.empty()) return std::string();   // several: the user chooses
        pick = found[i].ip;
    }
    return pick;
}
