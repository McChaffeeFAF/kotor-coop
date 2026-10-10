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

#include "reone/texgen/job.h"

#include "reone/graphics/format/tgareader.h"
#include "reone/graphics/format/tgawriter.h"
#include "reone/graphics/format/tpcreader.h"
#include "reone/graphics/format/txireader.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/textureutil.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace texgen {

namespace {

struct Settings {
    JobOptions options;
    std::string rulesFingerprint;
};

struct Result {
    Outcome outcome {Outcome::Generated};
    ManifestEntry entry;
    std::string detail;
    bool record {true}; ///< whether the manifest should hold the entry
};

constexpr char kToolVersion[] = "texgen-2";

// Only the output and override folders hold generated companion maps. A texture of the
// game's own archives is always a source, whatever its name ends in (n_comm_b_m).
bool isGeneratedCompanionFolder(const TextureSource &source, const JobOptions &options) {
    if (source.fromOverride) {
        return true;
    }
    if (source.origin.empty() || options.outputDir.empty()) {
        return false;
    }
    std::error_code ec;
    auto origin = std::filesystem::weakly_canonical(std::filesystem::path(source.origin), ec);
    auto output = std::filesystem::weakly_canonical(options.outputDir, ec);
    if (origin.empty() || output.empty()) {
        return false;
    }
    auto rel = origin.lexically_relative(output);
    return !rel.empty() && *rel.begin() != "..";
}

bool endsWith(const std::string &s, const char *suffix) {
    size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::filesystem::path outputPath(const JobOptions &options, const std::string &name, const char *suffix) {
    return options.outputDir / (name + suffix + (options.format == OutputFormat::Dds ? ".dds" : ".tga"));
}

// The same map in the other format: left behind it would shadow (a DDS) or be shadowed by the new file.
std::filesystem::path otherOutputPath(const JobOptions &options, const std::string &name, const char *suffix) {
    return options.outputDir / (name + suffix + (options.format == OutputFormat::Dds ? ".tga" : ".dds"));
}

void writeFile(const std::filesystem::path &path, const ByteBuffer &bytes) {
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("cannot write " + tmp.string());
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            throw std::runtime_error("cannot write " + tmp.string());
        }
    }
    std::filesystem::rename(tmp, path);
}

void writeTga(const std::filesystem::path &path, const Image &image) {
    TgaWriter writer(image.width, image.height, PixelFormat::RGB8, toBytes(image, 3));
    writeFile(path, writer.toBytes());
}

void writeDdsFile(const std::filesystem::path &path, const ByteBuffer &bytes) {
    writeFile(path, bytes);
}

void removeOutputs(const JobOptions &options, const std::string &name) {
    std::error_code ec;
    for (const char *suffix : {"_n", "_m"}) {
        std::filesystem::remove(outputPath(options, name, suffix), ec);
        std::filesystem::remove(otherOutputPath(options, name, suffix), ec);
    }
}

bool outputsExist(const JobOptions &options, const std::string &name) {
    std::error_code ec;
    return std::filesystem::exists(outputPath(options, name, "_n"), ec) &&
           std::filesystem::exists(outputPath(options, name, "_m"), ec);
}

// A texture whose alpha is nearly all on or off and has some of both: a cut-out.
bool alphaIsCutout(const Image &image) {
    size_t count = static_cast<size_t>(image.width) * image.height;
    size_t clear = 0;
    size_t binary = 0;
    for (size_t i = 0; i < count; ++i) {
        float a = image.rgba[i * 4 + 3];
        if (a < 0.1f) {
            ++clear;
            ++binary;
        } else if (a > 0.9f) {
            ++binary;
        }
    }
    return count > 0 && binary >= count * 9 / 10 && clear >= count / 50;
}

Result process(const TextureSource &source, const SourceSet &sources, const Rules &rules, const Settings &settings,
               const Manifest &manifest) {
    const auto &options = settings.options;
    Result result;
    result.entry.texture = source.name;

    auto skip = [&](const std::string &className, const std::string &reason, float strength = 0.0f) {
        result.outcome = Outcome::Skipped;
        result.entry.className = className;
        result.entry.status = "skipped";
        result.entry.reason = reason;
        result.entry.strength = strength;
        result.detail = reason;
    };

    ByteBuffer bytes = source.read();
    std::optional<ByteBuffer> looseTxi;
    if (source.type == ResType::Tga) {
        if (auto found = sources.txis.find(source.name); found != sources.txis.end()) {
            looseTxi = found->second();
        }
    }
    Hasher hasher;
    hasher.add(bytes.data(), bytes.size());
    if (looseTxi) {
        hasher.add(looseTxi->data(), looseTxi->size());
    }
    hasher.add(std::string("|") + kToolVersion + "|" + std::to_string(options.baseStrength) + "|" + settings.rulesFingerprint);
    hasher.add(options.format == OutputFormat::Dds ? (options.materialCodec == MaterialCodec::BC1 ? "|dds-bc1" : "|dds-bc7") : "|tga");
    // Whether a game texture of the companion name exists changes the answer too.
    hasher.add(sources.has(source.name + "_n") ? "|n" : "|-");
    result.entry.sourceHash = hasher.hex();

    if (!options.force) {
        auto old = manifest.entries.find(source.name);
        if (old != manifest.entries.end() && old->second.sourceHash == result.entry.sourceHash &&
            (old->second.status == "skipped" || (old->second.status == "generated" && outputsExist(options, source.name)))) {
            result.outcome = Outcome::Unchanged;
            result.entry = old->second;
            result.detail = old->second.status;
            return result;
        }
    }

    // Decode.
    std::shared_ptr<Texture> texture;
    Texture::Features features;
    if (source.type == ResType::Tpc) {
        MemoryInputStream stream(bytes);
        TpcReader reader(stream, source.name, TextureUsage::Default);
        reader.load();
        texture = reader.texture();
        features = texture->features();
    } else {
        MemoryInputStream stream(bytes);
        TgaReader reader(stream, source.name, TextureUsage::Default);
        reader.load();
        texture = reader.texture();
        if (looseTxi) {
            MemoryInputStream txiStream(*looseTxi);
            TxiReader txi;
            txi.load(txiStream);
            features = txi.features();
        }
    }
    if (!texture) {
        skip("unsupported", "unsupported texture file");
        return result;
    }
    if (texture->isCubeMap() || texture->layers().size() > 1) {
        skip("cubemap", "cube map");
        return result;
    }
    if (features.procedureType != Texture::ProcedureType::Invalid || features.numX * features.numY > 1) {
        skip("animated", "animated or procedural texture");
        return result;
    }
    Image image = imageFromTexture(*texture);

    TextureInfo info;
    info.name = source.name;
    info.width = image.width;
    info.height = image.height;
    info.hasAlpha = hasAlphaChannel(texture->pixelFormat());
    info.cutout = features.blending == Texture::Blending::PunchThrough || (info.hasAlpha && alphaIsCutout(image));
    info.decal = features.decal;
    info.font = features.numChars > 0;
    info.shiny = !features.envmapTexture.empty() || !features.bumpyShinyTexture.empty();

    // The game's own maps win.
    if (!features.bumpmapTexture.empty()) {
        skip("bumped", "has a game bump map (" + features.bumpmapTexture + ")");
        return result;
    }
    if (sources.has(source.name + "_n")) {
        skip("bumped", "a game texture named " + source.name + "_n exists");
        return result;
    }

    Decision decision = rules.classify(info, options.baseStrength);
    if (decision.action == Action::Skip) {
        skip(decision.className, "class " + decision.className + (decision.rule.empty() ? "" : " (" + decision.rule + ")"), decision.strength);
        return result;
    }

    result.entry.className = decision.className;
    result.entry.status = "generated";
    result.entry.strength = decision.strength;
    result.detail = decision.className;
    if (options.dryRun) {
        result.record = false;
        return result;
    }

    NormalOptions normal;
    normal.strength = decision.strength;
    MaterialOptions material;
    material.shiny = info.shiny;
    Image normalMap = generateNormalMap(image, normal);
    Image materialMap = generateMaterialMap(image, material);
    if (options.format == OutputFormat::Dds) {
        writeDdsFile(outputPath(options, source.name, "_n"), encodeNormalDds(normalMap));
        writeDdsFile(outputPath(options, source.name, "_m"), encodeMaterialDds(materialMap, options.materialCodec));
    } else {
        writeTga(outputPath(options, source.name, "_n"), normalMap);
        writeTga(outputPath(options, source.name, "_m"), materialMap);
    }
    std::error_code ec;
    for (const char *suffix : {"_n", "_m"}) {
        std::filesystem::remove(otherOutputPath(options, source.name, suffix), ec);
    }
    return result;
}

} // namespace

Summary runJob(const SourceSet &sources, const Rules &rules, const JobOptions &options,
               const std::function<void(const Progress &)> &onProgress) {
    if (!options.dryRun) {
        std::filesystem::create_directories(options.outputDir);
    }
    auto manifestPath = options.outputDir / kManifestName;
    Manifest manifest = options.dryRun && !std::filesystem::exists(manifestPath) ? Manifest() : Manifest::load(manifestPath);

    Settings settings;
    settings.options = options;
    settings.rulesFingerprint = rules.fingerprint();

    std::vector<const TextureSource *> work;
    for (const auto &[name, source] : sources.textures) {
        if (!options.only.empty() && !globMatch(options.only, name)) {
            continue;
        }
        work.push_back(&source);
    }

    Summary summary;
    summary.total = work.size();
    std::mutex mutex;
    std::atomic<size_t> next {0};
    size_t done = 0;
    size_t sinceSave = 0;

    auto finish = [&](const TextureSource &source, Result result) {
        std::lock_guard<std::mutex> lock(mutex);
        switch (result.outcome) {
        case Outcome::Generated:
            ++summary.generated;
            break;
        case Outcome::Skipped:
            ++summary.skipped;
            break;
        case Outcome::Unchanged:
            ++summary.unchanged;
            break;
        case Outcome::Failed:
            ++summary.failed;
            break;
        }
        if (result.outcome != Outcome::Failed && result.record && !options.dryRun) {
            if (result.outcome == Outcome::Skipped) {
                // A texture that used to get maps and no longer does gives them up.
                auto old = manifest.entries.find(source.name);
                if (old != manifest.entries.end() && old->second.status == "generated") {
                    removeOutputs(options, source.name);
                }
            }
            manifest.entries[source.name] = result.entry;
            if (++sinceSave >= 200) {
                sinceSave = 0;
                manifest.save(manifestPath);
            }
        }
        ++done;
        if (onProgress) {
            Progress progress;
            progress.done = done;
            progress.total = work.size();
            progress.texture = source.name;
            progress.outcome = result.outcome;
            progress.detail = result.detail;
            onProgress(progress);
        }
    };

    auto worker = [&]() {
        for (;;) {
            size_t index = next.fetch_add(1);
            if (index >= work.size()) {
                return;
            }
            const TextureSource &source = *work[index];
            Result result;
            if ((endsWith(source.name, "_n") || endsWith(source.name, "_m")) &&
                isGeneratedCompanionFolder(source, options)) {
                result.outcome = Outcome::Skipped;
                result.entry.texture = source.name;
                result.record = false;
                result.detail = "looks like a generated or companion map";
            } else {
                try {
                    // The manifest is only read here; writes happen under the lock after the read of
                    // this texture's entry, and each texture is handled once.
                    Manifest snapshotEntry;
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        auto old = manifest.entries.find(source.name);
                        if (old != manifest.entries.end()) {
                            snapshotEntry.entries[source.name] = old->second;
                        }
                    }
                    result = process(source, sources, rules, settings, snapshotEntry);
                } catch (const std::exception &e) {
                    result = Result();
                    result.outcome = Outcome::Failed;
                    result.record = false;
                    result.detail = e.what();
                }
            }
            finish(source, std::move(result));
        }
    };

    int threads = options.threads > 0 ? options.threads : static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    threads = std::min<int>(threads, static_cast<int>(std::max<size_t>(1, work.size())));
    std::vector<std::thread> pool;
    for (int i = 1; i < threads; ++i) {
        pool.emplace_back(worker);
    }
    worker();
    for (auto &t : pool) {
        t.join();
    }

    if (!options.dryRun) {
        manifest.save(manifestPath);
    }
    return summary;
}

} // namespace texgen

} // namespace reone
