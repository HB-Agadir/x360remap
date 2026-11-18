#pragma once
// "Watch until freeze": follows the console over xbdm and writes every line to
// the PC at once, so the last second before a freeze is never lost. No UI
// here: the Logs dialog, --watch and windows/tests drive it.
#include <stdint.h>
#include <string>
#include <vector>

namespace livewatch {

struct Params {
    std::string host, ftpUser, ftpPassword, remoteDir, outDir;
    int ftpPort, xbdmPort;
    Params() : ftpPort(21), xbdmPort(730) {}
};

struct Snapshot {
    bool running, notifyUp, consoleAlive;
    unsigned lines;
    std::string capturePath, reportPath, deathTime;
    std::vector<std::string> tail;      // last lines, oldest first
    Snapshot() : running(false), notifyUp(false), consoleAlive(false), lines(0) {}
};

void Start(const Params& p);
void Stop();
Snapshot Get();
// Saves a report now (a freeze with xbdm still answering). Blocking, a few
// seconds; a console that stops answering gets one by itself.
std::string SaveReport();

// Text of a plugin line from "debugstr thread=7 lf string=...", else the line.
std::string DebugStrText(const std::string& line);
// Thread id of an "exception ... thread=0x..." notification, else 0.
uint32_t ExceptionThread(const std::string& line);
// Value of  "name": "0000abcd"  in the status file; 0 when absent.
uint32_t JsonHex(const std::string& json, const char* name);
// Value of  name=0x1234  in getcontext lines, any case; false when absent.
bool ContextValue(const std::vector<std::string>& lines, const char* name, uint32_t& value);

}  // namespace livewatch
