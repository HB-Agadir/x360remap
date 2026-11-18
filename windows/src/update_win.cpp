#include "update_win.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <cstring>

#include <rapidjson/document.h>

#include "../../application/donation_links.h"

#pragma comment(lib, "winhttp.lib")

namespace {

std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

bool EndsWithI(const std::string& s, const char* tail) {
    const size_t n = strlen(tail);
    return s.size() >= n && _stricmp(s.c_str() + s.size() - n, tail) == 0;
}

// "github.com/HB-Agadir/X360Remap/releases/latest" -> "HB-Agadir/X360Remap"
std::string RepoFromReleasesUrl() {
    std::string u = IsLinkIntact(kReleasesUrl, kReleasesUrlHash) ? kReleasesUrl : "";
    const std::string::size_type a = u.find("github.com/"), b = u.find("/releases");
    if (a == std::string::npos || b == std::string::npos || b <= a + 11) return std::string();
    return u.substr(a + 11, b - a - 11);
}

}  // namespace

bool HttpGet(const std::string& url, std::string& body, std::string& err) {
    body.clear();
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = L"", path[2048] = L"";
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;  uc.dwUrlPathLength = 2048;
    const std::wstring wurl = Widen(url);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) { err = "invalid URL"; return false; }

    bool ok = false;
    HINTERNET s = WinHttpOpen(L"X360RemapStudio-Windows", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, uc.nPort, 0) : nullptr;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    if (r) WinHttpSetTimeouts(r, 10000, 10000, 15000, 30000);
    const wchar_t* headers = L"Accept: application/vnd.github+json\r\n";
    if (r && WinHttpSendRequest(r, headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(r, &avail) && avail > 0) {
            const size_t at = body.size();
            body.resize(at + avail);
            DWORD read = 0;
            if (!WinHttpReadData(r, &body[at], avail, &read)) break;
            body.resize(at + read);
            if (body.size() > 64u * 1024u * 1024u) break;   // a release zip is a few MB
        }
        ok = status == 200;
        if (!ok) { char buf[32]; sprintf(buf, "HTTP %lu", status); err = buf; }
    } else {
        err = "no answer from GitHub (no Internet?)";
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return ok;
}

bool ParseNewestRelease(const std::string& json, bool includePreviews, ReleaseInfo& out) {
    rapidjson::Document d;
    d.Parse(json.c_str());
    if (d.HasParseError() || !d.IsArray()) return false;
    for (rapidjson::SizeType i = 0; i < d.Size(); i++) {   // GitHub lists the newest first
        const rapidjson::Value& rel = d[i];
        if (!rel.IsObject() || !rel.HasMember("id") || !rel["id"].IsInt64()) continue;
        const bool pre = rel.HasMember("prerelease") && rel["prerelease"].IsBool() && rel["prerelease"].GetBool();
        const bool draft = rel.HasMember("draft") && rel["draft"].IsBool() && rel["draft"].GetBool();
        if (draft || (pre && !includePreviews)) continue;
        ReleaseInfo r;
        r.id = rel["id"].GetInt64();
        r.prerelease = pre;
        if (rel.HasMember("name") && rel["name"].IsString()) r.name = rel["name"].GetString();
        if (r.name.empty() && rel.HasMember("tag_name") && rel["tag_name"].IsString()) r.name = rel["tag_name"].GetString();
        if (rel.HasMember("published_at") && rel["published_at"].IsString()) r.publishedAt = rel["published_at"].GetString();
        if (rel.HasMember("html_url") && rel["html_url"].IsString()) r.pageUrl = rel["html_url"].GetString();
        if (rel.HasMember("assets") && rel["assets"].IsArray()) {
            const rapidjson::Value& assets = rel["assets"];
            for (rapidjson::SizeType k = 0; k < assets.Size(); k++) {
                if (!assets[k].HasMember("name") || !assets[k]["name"].IsString() ||
                    !assets[k].HasMember("browser_download_url") || !assets[k]["browser_download_url"].IsString()) continue;
                const std::string n = assets[k]["name"].GetString(), u = assets[k]["browser_download_url"].GetString();
                if (_stricmp(n.c_str(), "X360Remap.xex") == 0) r.pluginUrl = u;
                else if (_stricmp(n.c_str(), "X360RemapStudio.xex") == 0) r.studioUrl = u;
                // The Windows app's own zip ("...-win64.zip") sits in the same
                // release: never take it for the console files.
                else if (EndsWithI(n, ".zip") && r.zipUrl.empty() && !EndsWithI(n, "-win64.zip")) r.zipUrl = u;
            }
        }
        out = r;
        return true;
    }
    return false;
}

bool FetchNewestRelease(bool includePreviews, ReleaseInfo& out, std::string& err) {
    const std::string repo = RepoFromReleasesUrl();
    if (repo.empty()) { err = "release address missing"; return false; }
    std::string body;
    if (!HttpGet("https://api.github.com/repos/" + repo + "/releases?per_page=15", body, err)) return false;
    if (!ParseNewestRelease(body, includePreviews, out)) { err = "no release found"; return false; }
    return true;
}
