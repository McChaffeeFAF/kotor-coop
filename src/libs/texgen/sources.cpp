/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "reone/texgen/sources.h"

#include "reone/resource/format/bifreader.h"
#include "reone/resource/format/erfreader.h"
#include "reone/resource/format/keyreader.h"
#include "reone/resource/format/rimreader.h"
#include "reone/system/stream/fileinput.h"

using namespace reone::resource;

namespace reone {

namespace texgen {

bool SourceSet::add(TextureSource source) {
    auto name = source.name;
    return textures.emplace(std::move(name), std::move(source)).second;
}

void SourceSet::addTxi(const std::string &name, std::function<std::optional<ByteBuffer>()> read) {
    txis.emplace(name, std::move(read));
}

TextureSource memorySource(const std::string &name, ResType type, ByteBuffer bytes) {
    TextureSource source;
    source.name = boost::to_lower_copy(name);
    source.type = type;
    source.origin = "memory:" + name;
    auto shared = std::make_shared<ByteBuffer>(std::move(bytes));
    source.read = [shared]() { return *shared; };
    return source;
}

namespace {

std::string lower(const std::string &s) {
    return boost::to_lower_copy(s);
}

std::string extensionOf(const std::filesystem::path &path) {
    auto ext = lower(path.extension().string());
    return ext.empty() ? ext : ext.substr(1);
}

// A child of dir by name, ignoring case (installs copied to Linux keep their original case).
std::optional<std::filesystem::path> findChild(const std::filesystem::path &dir, const std::string &name) {
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        if (lower(entry.path().filename().string()) == lower(name)) {
            return entry.path();
        }
    }
    return std::nullopt;
}

// A relative path with either kind of separator, resolved piece by piece ignoring case.
std::optional<std::filesystem::path> resolveRelative(const std::filesystem::path &root, std::string relative) {
    std::replace(relative.begin(), relative.end(), '\\', '/');
    std::filesystem::path current = root;
    std::stringstream ss(relative);
    std::string piece;
    while (std::getline(ss, piece, '/')) {
        if (piece.empty() || piece == ".") {
            continue;
        }
        auto child = findChild(current, piece);
        if (!child) {
            return std::nullopt;
        }
        current = *child;
    }
    return current;
}

std::vector<std::filesystem::path> filesWithExtension(const std::filesystem::path &dir, std::initializer_list<const char *> exts) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto ext = extensionOf(entry.path());
        for (const char *wanted : exts) {
            if (ext == wanted) {
                files.push_back(entry.path());
                break;
            }
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

ByteBuffer readRange(const std::filesystem::path &path, uint64_t offset, uint64_t size) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    in.seekg(static_cast<std::streamoff>(offset));
    ByteBuffer bytes(static_cast<size_t>(size));
    in.read(bytes.data(), static_cast<std::streamsize>(size));
    if (static_cast<uint64_t>(in.gcount()) != size) {
        throw std::runtime_error("short read from " + path.string());
    }
    return bytes;
}

ByteBuffer readWhole(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    return ByteBuffer(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct Range {
    std::filesystem::path file;
    uint64_t offset {0};
    uint64_t size {0};
};

void addResource(SourceSet &set, const std::string &name, ResType type, const std::string &origin, Range range) {
    auto reader = [range]() { return readRange(range.file, range.offset, range.size); };
    if (type == ResType::Txi) {
        set.addTxi(name, [reader]() -> std::optional<ByteBuffer> { return reader(); });
        return;
    }
    TextureSource source;
    source.name = name;
    source.type = type;
    source.origin = origin;
    source.read = reader;
    set.add(std::move(source));
}

bool wanted(ResType type) {
    return type == ResType::Tpc || type == ResType::Tga || type == ResType::Txi;
}

void scanLooseFile(SourceSet &set, const std::filesystem::path &path, bool fromOverride = false) {
    auto ext = extensionOf(path);
    auto name = lower(path.stem().string());
    if (ext == "tpc" || ext == "tga") {
        TextureSource source;
        source.name = name;
        source.type = ext == "tpc" ? ResType::Tpc : ResType::Tga;
        source.origin = path.string();
        source.read = [path]() { return readWhole(path); };
        source.fromOverride = fromOverride;
        set.add(std::move(source));
    } else if (ext == "txi") {
        set.addTxi(name, [path]() -> std::optional<ByteBuffer> { return readWhole(path); });
    }
}

void scanErf(SourceSet &set, const std::filesystem::path &path) {
    try {
        FileInputStream stream(path);
        ErfReader erf(stream);
        erf.load();
        // TPCs before TGAs, so a TPC of the archive beats a TGA of the same name.
        for (int pass = 0; pass < 2; ++pass) {
            for (size_t i = 0; i < erf.keys().size() && i < erf.resources().size(); ++i) {
                const auto &id = erf.keys()[i].resId;
                if (!wanted(id.type) || (pass == 0) != (id.type == ResType::Tpc)) {
                    continue;
                }
                addResource(set, id.resRef.value(), id.type, path.string() + ":" + id.string(),
                            {path, erf.resources()[i].offset, erf.resources()[i].size});
            }
        }
    } catch (const std::exception &e) {
        set.warnings.push_back("skipped " + path.string() + ": " + e.what());
    }
}

void scanRim(SourceSet &set, const std::filesystem::path &path) {
    try {
        FileInputStream stream(path);
        RimReader rim(stream);
        rim.load();
        for (int pass = 0; pass < 2; ++pass) {
            for (const auto &res : rim.resources()) {
                if (!wanted(res.resId.type) || (pass == 0) != (res.resId.type == ResType::Tpc)) {
                    continue;
                }
                addResource(set, res.resId.resRef.value(), res.resId.type, path.string() + ":" + res.resId.string(),
                            {path, res.offset, res.size});
            }
        }
    } catch (const std::exception &e) {
        set.warnings.push_back("skipped " + path.string() + ": " + e.what());
    }
}

void scanKey(SourceSet &set, const std::filesystem::path &keyPath, const std::filesystem::path &root) {
    try {
        FileInputStream stream(keyPath);
        KeyReader key(stream);
        key.load();
        std::map<int, std::vector<BifReader::ResourceEntry>> bifs;
        std::map<int, std::filesystem::path> bifPaths;
        auto bifOf = [&](int index) -> const std::vector<BifReader::ResourceEntry> * {
            auto found = bifs.find(index);
            if (found != bifs.end()) {
                return &found->second;
            }
            bifs[index] = {};
            if (index < 0 || index >= static_cast<int>(key.files().size())) {
                return nullptr;
            }
            auto path = resolveRelative(root, key.files()[index].filename);
            if (!path) {
                set.warnings.push_back("missing archive " + key.files()[index].filename);
                return nullptr;
            }
            try {
                FileInputStream bifStream(*path);
                BifReader bif(bifStream);
                bif.load();
                bifs[index] = bif.resources();
                bifPaths[index] = *path;
            } catch (const std::exception &e) {
                set.warnings.push_back("skipped " + path->string() + ": " + e.what());
                return nullptr;
            }
            return &bifs[index];
        };
        for (int pass = 0; pass < 2; ++pass) {
            for (const auto &entry : key.keys()) {
                if (!wanted(entry.resId.type) || (pass == 0) != (entry.resId.type == ResType::Tpc)) {
                    continue;
                }
                auto *resources = bifOf(entry.bifIdx);
                if (!resources || entry.resIdx < 0 || entry.resIdx >= static_cast<int>(resources->size())) {
                    continue;
                }
                const auto &res = (*resources)[entry.resIdx];
                addResource(set, entry.resId.resRef.value(), entry.resId.type,
                            bifPaths[entry.bifIdx].string() + ":" + entry.resId.string(),
                            {bifPaths[entry.bifIdx], res.offset, res.fileSize});
            }
        }
    } catch (const std::exception &e) {
        set.warnings.push_back("skipped " + keyPath.string() + ": " + e.what());
    }
}

void scanArchiveFile(SourceSet &set, const std::filesystem::path &path, const std::filesystem::path &root) {
    auto ext = extensionOf(path);
    if (ext == "key") {
        scanKey(set, path, root);
    } else if (ext == "erf" || ext == "mod") {
        scanErf(set, path);
    } else if (ext == "rim") {
        scanRim(set, path);
    } else {
        scanLooseFile(set, path);
    }
}

} // namespace

SourceSet scanSources(const std::filesystem::path &path) {
    SourceSet set;
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        scanArchiveFile(set, path, path.parent_path());
        return set;
    }
    if (!std::filesystem::is_directory(path, ec)) {
        set.warnings.push_back("not a file or directory: " + path.string());
        return set;
    }
    // Override first, then modules, texture packs and the key/BIF archives.
    if (auto dir = findChild(path, "override")) {
        for (const auto &file : filesWithExtension(*dir, {"tpc"})) {
            scanLooseFile(set, file, true);
        }
        for (const auto &file : filesWithExtension(*dir, {"tga", "txi"})) {
            scanLooseFile(set, file, true);
        }
    }
    if (auto dir = findChild(path, "modules")) {
        for (const auto &file : filesWithExtension(*dir, {"mod", "rim", "erf"})) {
            scanArchiveFile(set, file, path);
        }
    }
    if (auto dir = findChild(path, "texturepacks")) {
        for (const auto &file : filesWithExtension(*dir, {"erf"})) {
            scanErf(set, file);
        }
    }
    if (auto key = findChild(path, "chitin.key")) {
        scanKey(set, *key, path);
    }
    // Loose textures straight in the directory (a folder of extracted files).
    for (const auto &file : filesWithExtension(path, {"tpc", "tga", "txi"})) {
        scanLooseFile(set, file);
    }
    return set;
}

} // namespace texgen

} // namespace reone
