#pragma once
// New releases on GitHub (VISION.md 4: optional, off by default, nothing sent
// but the request). Blocking calls: run them from the sync worker.
#include <string>

struct ReleaseInfo {
    long long   id;            // GitHub release id: "new" = an id not seen before
    std::string name;          // "X360Remap_1.3-preview_pre12"
    std::string publishedAt;   // "2026-10-18T10:15:41Z"
    std::string pageUrl;
    bool        prerelease;
    // Where the two .xex come from: a zip, or the two files attached directly.
    std::string zipUrl, pluginUrl, studioUrl;
    ReleaseInfo() : id(0), prerelease(false) {}
};

// Newest release of the X360Remap repository (the one named in
// donation_links.cpp), previews included or not. false + err on failure.
bool FetchNewestRelease(bool includePreviews, ReleaseInfo& out, std::string& err);

// Parses the GitHub "list releases" answer (newest first). Exposed for the tests.
bool ParseNewestRelease(const std::string& json, bool includePreviews, ReleaseInfo& out);

// HTTPS GET (redirects followed), whole body in memory.
bool HttpGet(const std::string& url, std::string& body, std::string& err);
