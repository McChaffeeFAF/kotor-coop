/* Copyright (c) 2026 The reone project contributors */

#include "reone/net/fingerprint.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <vector>

#include "reone/net/bytes.h"

namespace reone {

namespace net {

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

/** Case-insensitive lookup of one child: installs copied from Windows vary. */
std::filesystem::path findChild(const std::filesystem::path &dir, const std::string &name) {
    std::error_code ec;
    auto direct = dir / name;
    if (std::filesystem::exists(direct, ec)) {
        return direct;
    }
    if (!std::filesystem::is_directory(dir, ec)) {
        return {};
    }
    for (auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        if (lower(entry.path().filename().string()) == lower(name)) {
            return entry.path();
        }
    }
    return {};
}

std::string hex(uint64_t value) {
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    return buffer;
}

} // namespace

uint64_t hashFile(const std::filesystem::path &path) {
    if (path.empty()) {
        return 0;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return 0;
    }
    Fnv1a64 hash;
    std::vector<char> buffer(1 << 16);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        auto count = in.gcount();
        if (count > 0) {
            hash.update(buffer.data(), static_cast<size_t>(count));
        }
    }
    return hash.value();
}

Fingerprint fingerprintInstall(const std::filesystem::path &gamePath) {
    Fingerprint result;
    result.present = true;
    result.key = hashFile(findChild(gamePath, "chitin.key"));
    result.tlk = hashFile(findChild(gamePath, "dialog.tlk"));

    std::vector<std::pair<std::string, uint64_t>> modules;
    auto modulesDir = findChild(gamePath, "modules");
    std::error_code ec;
    if (!modulesDir.empty() && std::filesystem::is_directory(modulesDir, ec)) {
        for (auto &entry : std::filesystem::directory_iterator(modulesDir, ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            auto size = static_cast<uint64_t>(entry.file_size(ec));
            modules.emplace_back(lower(entry.path().filename().string()), size);
        }
    }
    std::sort(modules.begin(), modules.end());
    Fnv1a64 list;
    list.updateU64(modules.size());
    for (auto &[name, size] : modules) {
        list.update(name);
        list.updateU64(size);
    }
    result.modules = list.value();
    return result;
}

std::string describeFingerprint(const Fingerprint &f) {
    if (!f.present) {
        return "absent";
    }
    return "key=" + hex(f.key) + " modules=" + hex(f.modules) + " tlk=" + hex(f.tlk);
}

bool installsCompatible(const Fingerprint &a, const Fingerprint &b) {
    return a.present && b.present && a.key == b.key && a.modules == b.modules;
}

bool tlkDiffers(const Fingerprint &a, const Fingerprint &b) {
    return a.present && b.present && a.tlk != b.tlk;
}

std::string fingerprintDifference(const Fingerprint &a, const Fingerprint &b) {
    if (a.present != b.present) {
        return "presence";
    }
    if (a.key != b.key) {
        return "key";
    }
    if (a.modules != b.modules) {
        return "modules";
    }
    if (a.tlk != b.tlk) {
        return "tlk";
    }
    return "";
}

} // namespace net

} // namespace reone
