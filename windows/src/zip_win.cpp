// plat::ExtractZip, in its own file so the tests can build it without ImGui.
// Unzips in the process with the inflate code stb_image already carries:
// starting tar.exe made antivirus engines read the exe as a dropper.
#include "platform_win.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>

#include "../../stb/stb_image.h"

namespace {

uint32_t Get16(const std::string& z, size_t p) { return (uint8_t)z[p] | ((uint8_t)z[p + 1] << 8); }
uint32_t Get32(const std::string& z, size_t p) { return Get16(z, p) | (Get16(z, p + 2) << 16); }

uint32_t Crc32(const char* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint8_t)data[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

std::wstring Widen(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// The zip comes from the internet: no absolute path, drive or "..".
bool SafeEntryName(const std::string& name) {
    if (name.empty() || name[0] == '/' || name[0] == '\\' || name.find(':') != std::string::npos) return false;
    size_t start = 0;
    while (start <= name.size()) {
        size_t end = name.find_first_of("/\\", start);
        if (end == std::string::npos) end = name.size();
        if (name.compare(start, end - start, "..") == 0) return false;
        start = end + 1;
    }
    return true;
}

bool WriteFileW(const std::wstring& path, const char* data, size_t len) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = len == 0 || (WriteFile(h, data, (DWORD)len, &written, nullptr) && written == len);
    CloseHandle(h);
    return ok;
}

}  // namespace

namespace plat {

bool ExtractZip(const std::string& zipPath, const std::string& destDir) {
    std::ifstream in(Widen(zipPath).c_str(), std::ios::binary);
    if (!in) return false;
    const std::string z((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // End of central directory: last "PK\5\6" (a comment may follow it).
    size_t eocd = std::string::npos;
    for (size_t p = z.size() >= 22 ? z.size() - 22 : 0; z.size() >= 22; p--) {
        if (Get32(z, p) == 0x06054b50u) { eocd = p; break; }
        if (p == 0 || z.size() - p > 22 + 65535) break;
    }
    if (eocd == std::string::npos) return false;
    const uint32_t count = Get16(z, eocd + 10);
    size_t cd = Get32(z, eocd + 16);

    const std::wstring root = Widen(destDir);
    CreateDirectoryW(root.c_str(), nullptr);
    for (uint32_t i = 0; i < count; i++) {
        if (cd + 46 > z.size() || Get32(z, cd) != 0x02014b50u) return false;
        const uint32_t method = Get16(z, cd + 10), crc = Get32(z, cd + 16);
        const uint32_t packed = Get32(z, cd + 20), size = Get32(z, cd + 24);
        const uint32_t nameLen = Get16(z, cd + 28), extraLen = Get16(z, cd + 30), commentLen = Get16(z, cd + 32);
        const size_t local = Get32(z, cd + 42);
        if (cd + 46 + nameLen > z.size()) return false;
        const std::string name = z.substr(cd + 46, nameLen);
        cd += 46 + nameLen + extraLen + commentLen;
        if (!SafeEntryName(name)) return false;

        // Folders on the way (also for entries without their own folder entry).
        std::wstring path = root;
        size_t start = 0, slash;
        while ((slash = name.find_first_of("/\\", start)) != std::string::npos) {
            path += L"\\" + Widen(name.substr(start, slash - start));
            CreateDirectoryW(path.c_str(), nullptr);
            start = slash + 1;
        }
        if (start == name.size()) continue;   // folder entry
        path += L"\\" + Widen(name.substr(start));

        if (local + 30 > z.size() || Get32(z, local) != 0x04034b50u) return false;
        const size_t data = local + 30 + Get16(z, local + 26) + Get16(z, local + 28);
        if (data + packed > z.size()) return false;
        std::string out(size, '\0');
        if (method == 0) {
            if (packed != size) return false;
            if (size) memcpy(&out[0], z.data() + data, size);
        } else if (method == 8) {
            if (size && stbi_zlib_decode_noheader_buffer(&out[0], (int)size, z.data() + data, (int)packed) != (int)size)
                return false;
        } else {
            return false;   // no other compression in a GitHub release zip
        }
        if (Crc32(out.data(), out.size()) != crc || !WriteFileW(path, out.data(), out.size())) return false;
    }
    return true;
}

}  // namespace plat
