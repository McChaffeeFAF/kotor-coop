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

#pragma once

#include "compress.h"
#include "generate.h"
#include "manifest.h"
#include "rules.h"
#include "sources.h"

namespace reone {

namespace texgen {

struct JobOptions {
    std::filesystem::path outputDir;
    float baseStrength {2.0f};
    int threads {0};     ///< 0: one per core
    bool force {false};  ///< regenerate even where the manifest says nothing changed
    bool dryRun {false}; ///< classify and report, write nothing
    std::string only;    ///< a glob; when set, only textures matching it are looked at
    OutputFormat format {OutputFormat::Dds};
    MaterialCodec materialCodec {MaterialCodec::BC7}; ///< DDS output only
};

enum class Outcome {
    Generated,
    Skipped,   ///< the rules, or the texture itself, say no map is wanted
    Unchanged, ///< the manifest already has this texture's outputs
    Failed
};

struct Progress {
    size_t done {0};
    size_t total {0};
    std::string texture;
    Outcome outcome {Outcome::Generated};
    std::string detail; ///< the class, the skip reason or the error
};

struct Summary {
    size_t total {0};
    size_t generated {0};
    size_t skipped {0};
    size_t unchanged {0};
    size_t failed {0};
};

constexpr char kManifestName[] = "texgen_manifest.json";

/**
 * Generates the normal (`<name>_n.dds` or `.tga`) and material (`<name>_m.dds` or `.tga`) maps for the textures of a
 * source set into the output directory, on several threads, and records them in the manifest.
 * `onProgress` is called once per texture, from whichever thread finished it, one call at a time.
 */
Summary runJob(const SourceSet &sources, const Rules &rules, const JobOptions &options,
               const std::function<void(const Progress &)> &onProgress = nullptr);

} // namespace texgen

} // namespace reone
