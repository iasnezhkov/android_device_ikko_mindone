/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#include "NvcfgJson.h"

#include <fcntl.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace mindone {
namespace hf {
namespace nvcfg_json {

namespace {

void skipWs(const std::string& s, size_t* i) {
    while (*i < s.size() && std::isspace(static_cast<unsigned char>(s[*i]))) ++*i;
}

// Directory part of a path, for the fsync-the-directory step in
// writeIntArray(). "/a/b/c" -> "/a/b"; "c" -> ".".
std::string dirnameOf(const std::string& path) {
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

}  // namespace

std::optional<std::vector<int32_t>> readIntArray(const std::string& path,
                                                   const std::string& expectedKey,
                                                   bool* outKeyMismatch,
                                                   std::string* outActualKey) {
    if (outKeyMismatch) *outKeyMismatch = false;

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return std::nullopt;  // ENOENT etc. - expected/common, not an error
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();

    size_t i = 0;
    skipWs(text, &i);
    if (i >= text.size() || text[i] != '{') return std::nullopt;
    ++i;
    skipWs(text, &i);
    if (i >= text.size() || text[i] != '"') return std::nullopt;
    ++i;
    size_t keyStart = i;
    while (i < text.size() && text[i] != '"') ++i;
    if (i >= text.size()) return std::nullopt;
    const std::string key = text.substr(keyStart, i - keyStart);
    ++i;  // closing quote
    skipWs(text, &i);
    if (i >= text.size() || text[i] != ':') return std::nullopt;
    ++i;
    skipWs(text, &i);
    if (i >= text.size() || text[i] != '[') return std::nullopt;
    ++i;

    if (outActualKey) *outActualKey = key;
    if (outKeyMismatch) *outKeyMismatch = (key != expectedKey);

    std::vector<int32_t> values;
    skipWs(text, &i);
    if (i < text.size() && text[i] == ']') {
        ++i;  // empty array
    } else {
        for (;;) {
            skipWs(text, &i);
            size_t numStart = i;
            if (i < text.size() && (text[i] == '-' || text[i] == '+')) ++i;
            size_t digitsStart = i;
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
            if (i == digitsStart) return std::nullopt;  // no digits - malformed
            errno = 0;
            long long v = std::strtoll(text.substr(numStart, i - numStart).c_str(), nullptr, 10);
            if (errno == ERANGE) return std::nullopt;
            values.push_back(static_cast<int32_t>(v));
            skipWs(text, &i);
            if (i < text.size() && text[i] == ',') {
                ++i;
                continue;
            }
            if (i < text.size() && text[i] == ']') {
                ++i;
                break;
            }
            return std::nullopt;  // neither ',' nor ']' - malformed
        }
    }
    skipWs(text, &i);
    if (i >= text.size() || text[i] != '}') return std::nullopt;

    return values;
}

std::optional<std::string> writeIntArrayTmp(const std::string& path, const std::string& key,
                                             const std::vector<int32_t>& values) {
    std::ostringstream out;
    out << "{\n  \"" << key << "\": [\n";
    for (size_t i = 0; i < values.size(); ++i) {
        out << "      " << values[i];
        if (i + 1 < values.size()) out << ",";
        out << "\n";
    }
    out << "    ]\n}\n";
    const std::string text = out.str();

    const std::string tmpPath = path + ".tmp";
    int fd = ::open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return std::nullopt;

    bool ok = true;
    size_t written = 0;
    while (written < text.size()) {
        ssize_t n = ::write(fd, text.data() + written, text.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            ok = false;
            break;
        }
        written += static_cast<size_t>(n);
    }
    if (ok) ok = (::fsync(fd) == 0);
    ::close(fd);
    if (!ok) {
        ::unlink(tmpPath.c_str());
        return std::nullopt;
    }
    return tmpPath;
}

bool commitAtomicWrite(const std::string& tmpPath, const std::string& path) {
    if (::rename(tmpPath.c_str(), path.c_str()) != 0) {
        ::unlink(tmpPath.c_str());
        return false;
    }

    // Best-effort: fsync the containing directory too, so the rename() is
    // durable across a power loss and doesn't require an fsck to reappear.
    // nvcfg is a small dedicated ext4 partition (24MB, F-series device dump
    // showed ~76K used) mounted with commit=1 (see rootdir/etc/init.mt6789.rc
    // fstab) - this matters more there than on a typical /data mount.
    int dfd = ::open(dirnameOf(path).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        ::fsync(dfd);
        ::close(dfd);
    }
    return true;
}

bool writeIntArray(const std::string& path, const std::string& key,
                    const std::vector<int32_t>& values) {
    auto tmpPath = writeIntArrayTmp(path, key, values);
    if (!tmpPath) return false;
    return commitAtomicWrite(*tmpPath, path);
}

}  // namespace nvcfg_json
}  // namespace hf
}  // namespace mindone
