#include "live_watch.h"
#include "console_link.h"

#include <windows.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <share.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>

namespace livewatch {
namespace {

const size_t kTailLines = 400;
const uint32_t kLogBufferMax = 8192;     // LOG_BUFFER_SIZE in X360Remap/main.cpp
const int kFailsBeforeDead = 3;

Params                  g_params;
std::thread             g_notifyThread, g_pollThread;
std::atomic<bool>       g_stop(false);
std::mutex              g_lock;          // everything below
Snapshot                g_snap;
std::deque<std::string> g_tail;
FILE*                   g_file = nullptr;
std::string             g_logBuffer;     // last copy of the plugin's RAM log
std::string             g_lastText;      // repeated lines are counted, not written
unsigned                g_repeats = 0;
std::atomic<uint32_t>   g_faultThread(0);    // first thread seen faulting
std::atomic<bool>       g_rebooting(false);  // the console said so: silence is not a freeze

std::string Stamp(bool forFileName) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[40];
    if (forFileName) sprintf_s(buf, "%04d%02d%02d_%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    else             sprintf_s(buf, "%02d:%02d:%02d.%03d", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return buf;
}

void WriteLine(const std::string& line) {
    if (g_file) { fprintf(g_file, "%s\n", line.c_str()); fflush(g_file); }
    g_tail.push_back(line);
    if (g_tail.size() > kTailLines) g_tail.pop_front();
    g_snap.lines++;
}

// 2026-10-30: one faulting thread sent 423000 identical lines in 12 s.
void AddLine(const std::string& text) {
    std::lock_guard<std::mutex> hold(g_lock);
    if (text == g_lastText) { g_repeats++; return; }
    if (g_repeats) {
        char note[80];
        sprintf_s(note, "[watch] previous line repeated %u more times", g_repeats);
        WriteLine(Stamp(false) + " " + note);
    }
    g_repeats = 0;
    g_lastText = text;
    WriteLine(Stamp(false) + " " + text);
}

void SetFlag(bool Snapshot::*field, bool value) {
    std::lock_guard<std::mutex> hold(g_lock);
    g_snap.*field = value;
}

// The socket itself turns into the notification stream, so the PC opens no
// port. No "debugger connect": on 2026-10-30 both boots with it froze at the
// USB reset, and the boots without it did not.
void NotifyLoop() {
    while (!g_stop) {
        XbdmConn conn;
        std::string err, status;
        std::vector<std::string> lines;
        if (!conn.Connect(g_params.host.c_str(), g_params.xbdmPort, 2000, err) ||
            !conn.Command("notify reconnectport=1", status, lines) || status.empty() || status[0] != '2') {
            Sleep(500);
            continue;
        }
        SetFlag(&Snapshot::notifyUp, true);
        AddLine("[watch] notification stream open: " + status);
        while (!g_stop) {
            std::string line;
            bool timedOut = false;
            if (conn.ReadLine(line, 300, timedOut)) {
                AddLine(DebugStrText(line));
                if (!g_faultThread) g_faultThread = ExceptionThread(line);
                // a rebooting console never closes the socket: open a new stream
                if (line == "execution rebooting") { g_rebooting = true; break; }
            } else if (!timedOut) break;
        }
        SetFlag(&Snapshot::notifyUp, false);
        if (!g_stop) AddLine("[watch] notification stream closed");
    }
}

// The addresses change with each plugin build and only the status file gives them.
void ReadAddresses(uint32_t& logBuffer, uint32_t& logBufferLen) {
    FtpClient ftp;
    std::string err, json;
    bool found = false;
    if (!ftp.Connect(g_params.host.c_str(), g_params.ftpPort, g_params.ftpUser.c_str(), g_params.ftpPassword.c_str(), err)) return;
    if (!ftp.Download(JoinRemote(g_params.remoteDir, "X360Remap_status.json"), json, found, err) || !found) return;
    logBuffer = JsonHex(json, "logBuffer");
    logBufferLen = JsonHex(json, "logBufferLen");
}

void CopyLogBuffer(XbdmConn& conn, uint32_t logBuffer, uint32_t logBufferLen) {
    std::string raw, text;
    if (!logBuffer || !logBufferLen || !conn.GetMem(logBufferLen, 4, raw) || raw.size() != 4) return;
    const uint32_t len = ((uint8_t)raw[0] << 24) | ((uint8_t)raw[1] << 16) | ((uint8_t)raw[2] << 8) | (uint8_t)raw[3];
    if (len == 0 || len > kLogBufferMax || !conn.GetMem(logBuffer, len, text)) return;
    std::lock_guard<std::mutex> hold(g_lock);
    g_logBuffer = text;
}

// isstopped, registers and return addresses of one thread.
std::string DumpThread(XbdmConn& conn, uint32_t id) {
    std::string text, status;
    std::vector<std::string> lines;
    char cmd[80];
    sprintf_s(cmd, "isstopped thread=0x%x", id);
    conn.Command(cmd, status, lines);
    text += status + "\n";
    sprintf_s(cmd, "getcontext thread=0x%x control int", id);
    if (!conn.Command(cmd, status, lines)) return text;
    text += status + "\n";
    for (size_t k = 0; k < lines.size(); k++) text += "  " + lines[k] + "\n";
    // SVR4 frames: back chain at +0, the caller's LR at +4 of that frame
    uint32_t sp = 0;
    if (!ContextValue(lines, "r1", sp) && !ContextValue(lines, "gpr1", sp)) return text;
    text += "  stack:";
    for (int depth = 0; depth < 16 && sp; depth++) {
        std::string frame;
        if (!conn.GetMem(sp, 8, frame)) break;
        const uint8_t* b = (const uint8_t*)frame.data();
        sp = (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
        char hex[16];
        sprintf_s(hex, " %08x", (uint32_t)((b[4] << 24) | (b[5] << 16) | (b[6] << 8) | b[7]));
        text += hex;
    }
    return text + "\n";
}

std::string WriteReport() {
    std::string text = "X360Remap freeze report, saved " + Stamp(false) + "\n";
    {
        std::lock_guard<std::mutex> hold(g_lock);
        text += "time of death: " + (g_snap.deathTime.empty() ? std::string("console still answering") : g_snap.deathTime) + "\n";
        text += "capture: " + g_snap.capturePath + "\n\n--- plugin RAM log (last copy) ---\n" + g_logBuffer;
        text += "\n--- last lines received ---\n";
        for (size_t i = 0; i < g_tail.size(); i++) text += g_tail[i] + "\n";
    }
    // A hang that keeps xbdm alive: who is stuck, and where.
    XbdmConn conn;
    std::string err, status;
    std::vector<std::string> threads, lines;
    if (conn.Connect(g_params.host.c_str(), g_params.xbdmPort, 2000, err) && conn.Command("threads", status, threads)) {
        text += "\n--- threads ---\n";
        for (size_t i = 0; i < threads.size(); i++) {
            text += "thread " + threads[i] + ": " + DumpThread(conn, (uint32_t)strtoul(threads[i].c_str(), nullptr, 0));
        }
    } else {
        text += "\n(xbdm does not answer: no thread states)\n";
    }

    const std::string path = g_params.outDir + "\\freeze_report_" + Stamp(true) + ".txt";
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") == 0 && f) { fwrite(text.data(), 1, text.size(), f); fclose(f); }
    std::lock_guard<std::mutex> hold(g_lock);
    g_snap.reportPath = path;
    return path;
}

void PollLoop() {
    uint32_t logBuffer = 0, logBufferLen = 0;
    int fails = 0, tick = 0;
    bool wasAlive = false, reported = false, faultDumped = false;
    XbdmConn conn;
    while (!g_stop) {
        std::string err, status;
        std::vector<std::string> lines;
        const bool alive = (conn.IsOpen() || conn.Connect(g_params.host.c_str(), g_params.xbdmPort, 1500, err)) &&
                           conn.Command("threads", status, lines);
        if (alive) {
            fails = 0;
            reported = false;
            if (!wasAlive) AddLine("[watch] console answers");
            wasAlive = true;
            g_rebooting = false;
            SetFlag(&Snapshot::consoleAlive, true);
            { std::lock_guard<std::mutex> hold(g_lock); g_snap.deathTime.clear(); }
            // once, while the console still answers: it may not last
            if (g_faultThread && !faultDumped) {
                faultDumped = true;
                AddLine("[watch] faulting thread:\n" + DumpThread(conn, g_faultThread));
            }
            if (!logBuffer && tick % 5 == 0) ReadAddresses(logBuffer, logBufferLen);
            CopyLogBuffer(conn, logBuffer, logBufferLen);
        } else if (g_rebooting) {
            if (wasAlive) AddLine("[watch] console rebooting");
            wasAlive = false;
            fails = 0;
            SetFlag(&Snapshot::consoleAlive, false);
        } else if (wasAlive && ++fails >= kFailsBeforeDead) {
            const std::string when = Stamp(false);
            wasAlive = false;
            {
                std::lock_guard<std::mutex> hold(g_lock);
                g_snap.consoleAlive = false;
                g_snap.deathTime = when;
            }
            AddLine("[watch] console stopped answering");
            if (!reported) WriteReport();
            reported = true;
            logBuffer = logBufferLen = 0;   // the next boot may be another build
        }
        tick++;
        for (int i = 0; i < 10 && !g_stop; i++) Sleep(100);
    }
}

}  // namespace

uint32_t ExceptionThread(const std::string& line) {
    if (line.compare(0, 10, "exception ") != 0) return 0;
    const size_t at = line.find("thread=");
    return at == std::string::npos ? 0 : (uint32_t)strtoul(line.c_str() + at + 7, nullptr, 0);
}

std::string DebugStrText(const std::string& line) {
    if (line.compare(0, 9, "debugstr ") != 0) return line;
    const size_t at = line.find("string=");
    return at == std::string::npos ? line : line.substr(at + 7);
}

uint32_t JsonHex(const std::string& json, const char* name) {
    const std::string key = std::string("\"") + name + "\"";
    size_t at = json.find(key);
    if (at == std::string::npos) return 0;
    at = json.find_first_of("0123456789abcdefABCDEF", json.find(':', at));
    return at == std::string::npos ? 0 : (uint32_t)strtoul(json.c_str() + at, nullptr, 16);
}

// shortcut: the 360's exact getcontext names are not confirmed yet, so "r1"
// and "gpr1" are both tried; fix the name after the first real report.
bool ContextValue(const std::vector<std::string>& lines, const char* name, uint32_t& value) {
    const std::string key = std::string(name) + "=";
    for (size_t i = 0; i < lines.size(); i++) {
        const char* s = lines[i].c_str();
        for (const char* p = s; *p; p++) {
            if ((p != s && p[-1] != ' ') || _strnicmp(p, key.c_str(), key.size()) != 0) continue;
            value = (uint32_t)strtoul(p + key.size(), nullptr, 0);
            return true;
        }
    }
    return false;
}

void Start(const Params& p) {
    Stop();
    g_params = p;
    g_stop = false;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        g_snap = Snapshot();
        g_tail.clear();
        g_logBuffer.clear();
        g_lastText.clear();
        g_repeats = 0;
        g_faultThread = 0;
        g_rebooting = false;
        g_snap.running = true;
        g_snap.capturePath = p.outDir + "\\live_capture_" + Stamp(true) + ".txt";
        g_file = _fsopen(g_snap.capturePath.c_str(), "ab", _SH_DENYWR);   // readable while it runs
    }
    g_notifyThread = std::thread(NotifyLoop);
    g_pollThread = std::thread(PollLoop);
}

void Stop() {
    g_stop = true;
    if (g_notifyThread.joinable()) g_notifyThread.join();
    if (g_pollThread.joinable()) g_pollThread.join();
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_file) fclose(g_file);
    g_file = nullptr;
    g_snap.running = false;
}

Snapshot Get() {
    std::lock_guard<std::mutex> hold(g_lock);
    Snapshot out = g_snap;
    out.tail.assign(g_tail.begin(), g_tail.end());
    return out;
}

std::string SaveReport() { return WriteReport(); }

}  // namespace livewatch
