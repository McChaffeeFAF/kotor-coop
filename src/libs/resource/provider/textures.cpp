/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include "reone/resource/provider/textures.h"

#include "reone/system/frameperf.h"
#include "reone/system/renderdiag.h"

#include <chrono>

#include "reone/graphics/format/curreader.h"
#include "reone/graphics/format/tgareader.h"
#include "reone/graphics/format/tpcreader.h"
#include "reone/graphics/format/txireader.h"
#include "reone/graphics/options.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/textureutil.h"
#include "reone/graphics/types.h"
#include "reone/resource/resources.h"
#include "reone/system/logutil.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/threadutil.h"

using namespace reone::graphics;

namespace reone {

namespace resource {

void Textures::init() {
}

void Textures::clear() {
    _cache.clear();
    _surfaceCache.clear();
    ++_surfaceGeneration;
}

std::shared_ptr<Texture> Textures::get(const std::string &resRef, TextureUsage usage) {
    // "null" (MDL) and "****" (2DA) are the data's spellings of "none".
    if (resRef.empty() || resRef == "null" || resRef == "NULL" || resRef == "****") {
        return nullptr;
    }
    // The cache is keyed lower case, so look up lower case too: a mixed-case
    // name (environment maps named in material data, for one) used to miss
    // every time and reload and re-decode the texture on the main thread.
    std::string lcResRef(boost::to_lower_copy(resRef));
    auto maybeTexture = _cache.find(lcResRef);
    if (maybeTexture != _cache.end()) {
        return maybeTexture->second;
    }
    auto inserted = _cache.insert(std::make_pair(lcResRef, doGet(lcResRef, usage)));

    return inserted.first->second;
}

std::shared_ptr<Texture> Textures::doGet(const std::string &resRef, TextureUsage usage) {
    frameperf::Scope perfScope(frameperf::Phase::Resource, resRef);
    std::shared_ptr<Texture> texture;
    std::optional<Texture::Features> features;

    auto txiRes = _resources.find(ResourceId(resRef, ResType::Txi));
    if (txiRes) {
        auto txi = MemoryInputStream(txiRes->data);
        auto txiReader = TxiReader();
        txiReader.load(txi);
        features = txiReader.features();
    }

    auto tgaRes = _resources.find(ResourceId(resRef, ResType::Tga));
    if (tgaRes) {
        auto tga = MemoryInputStream(tgaRes->data);
        auto tgaReader = TgaReader(tga, resRef, usage);
        tgaReader.load();
        texture = tgaReader.texture();
        if (texture && features) {
            texture->setFeatures(*features);
        }
    }

    if (!texture) {
        auto tpcRes = _resources.find(ResourceId(resRef, ResType::Tpc));
        if (tpcRes) {
            auto tpc = MemoryInputStream(tpcRes->data);
            auto tpcReader = TpcReader(tpc, resRef, usage);
            tpcReader.load();
            texture = tpcReader.texture();
            if (texture) {
                if (features) {
                    texture->setFeatures(*features);
                } else {
                    features = texture->features();
                }
            }
        }
    }

    if (texture) {
        if (features &&
            features->procedureType != Texture::ProcedureType::Invalid &&
            (features->numX > 1 || features->numY > 1)) {
            convertGridTextureToArray(*texture, features->numX, features->numY);
        }
        texture->setAnisotropy(std::max(1.0f, exp2f(static_cast<float>(_options.anisotropicFiltering))));
        texture->init();
    } else {
        warn("Texture not found: " + resRef, LogChannel::Graphics);
        renderdiag::report("texture", resRef, "Texture not found: " + resRef);
    }

    return texture;
}

ITextures::SurfaceDetailState Textures::surfaceDetail() {
    if (_options.normalMaps != _surfaceEnabled ||
        _options.normalMapTest != _surfaceTest ||
        _options.normalMapFolder != _surfaceFolder) {
        _surfaceEnabled = _options.normalMaps;
        _surfaceTest = _options.normalMapTest;
        _surfaceFolder = _options.normalMapFolder;
        _surfaceCache.clear();
        _surfaceFound = 0;
        _surfaceMissing = 0;
        _surfaceMissingSample.clear();
        _surfaceRejected = 0;
        _surfaceLoadMicros = 0;
        ++_surfaceGeneration;
    }
    SurfaceDetailState state;
    state.generation = _surfaceGeneration;
    state.enabled = _surfaceEnabled;
    state.strength = glm::clamp(_options.normalMapStrength, 0.0f, 1.0f);
    return state;
}

std::shared_ptr<Texture> Textures::getSurfaceMap(const std::string &texture, surfacedetail::MapKind kind) {
    if (!surfaceDetail().enabled || texture.empty()) {
        return nullptr;
    }
    auto key = surfacedetail::mapFileName(texture, kind);
    auto it = _surfaceCache.find(key);
    if (it != _surfaceCache.end()) {
        return it->second;
    }
    auto start = std::chrono::steady_clock::now();
    auto map = loadSurfaceMap(texture, kind);
    _surfaceLoadMicros += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    _surfaceCache.insert({key, map});
    return map;
}

std::shared_ptr<Texture> Textures::loadSurfaceMap(const std::string &texture, surfacedetail::MapKind kind) {
    auto name = surfacedetail::mapFileName(texture, kind);
    std::shared_ptr<Texture> map;
    if (_surfaceTest) {
        static constexpr int kTestMapSize = 256;
        auto rgba = kind == surfacedetail::MapKind::Normal
                        ? surfacedetail::proceduralNormalMap(texture, kTestMapSize)
                        : surfacedetail::proceduralMaterialMap(texture, kTestMapSize);
        auto pixels = std::make_shared<ByteBuffer>(rgba.begin(), rgba.end());
        map = std::make_shared<Texture>(name, TextureType::TwoDim, getTextureProperties(TextureUsage::BumpMap));
        map->setPixels(kTestMapSize, kTestMapSize, PixelFormat::RGBA8, Texture::Layer {std::move(pixels)});
    } else {
        // Only ever read: the folder is the player's own, outside the game install.
        auto found = surfacedetail::findMapFile(_surfaceFolder, texture, kind);
        if (!found) {
            ++_surfaceMissing;
            if (_surfaceMissingSample.size() < 8) {
                _surfaceMissingSample.push_back(name);
            }
            return nullptr;
        }
        const auto &path = *found;
        try {
            std::ifstream file(path, std::ios::binary);
            ByteBuffer bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            auto stream = MemoryInputStream(bytes);
            auto reader = TgaReader(stream, name, TextureUsage::BumpMap);
            reader.load();
            map = reader.texture();
        } catch (const std::exception &ex) {
            warn("Surface map not loaded: " + path.string() + ": " + ex.what(), LogChannel::Graphics);
            map = nullptr;
        }
        if (!map || !map->is2D() || map->isGrayscale()) {
            // A height map or a cube map is not a tangent-space map: the
            // surface keeps its plain normal.
            ++_surfaceRejected;
            return nullptr;
        }
    }
    map->setAnisotropy(std::max(1.0f, exp2f(static_cast<float>(_options.anisotropicFiltering))));
    map->init();
    ++_surfaceFound;
    return map;
}

std::string Textures::surfaceDetailJson() {
    auto state = surfaceDetail();
    const auto &tangents = surfacedetail::tangentStats();
    std::string folder = _surfaceFolder;
    boost::replace_all(folder, "\\", "/");
    boost::replace_all(folder, "\"", "'");
    std::string sample;
    for (const auto &name : _surfaceMissingSample) {
        sample += (sample.empty() ? "\"" : ",\"") + name + "\"";
    }
    return str(boost::format("{\"enabled\":%d,\"test\":%d,\"strength\":%g,\"folder\":\"%s\",\"generation\":%d,"
                             "\"found\":%d,\"missing\":%d,\"rejected\":%d,\"load_ms\":%.3f,"
                             "\"tangent_meshes\":%d,\"tangent_vertices\":%d,\"tangent_ms\":%.3f,\"missing_sample\":[%s]}") %
               (state.enabled ? 1 : 0) % (_surfaceTest ? 1 : 0) % state.strength % folder % state.generation %
               _surfaceFound % _surfaceMissing % _surfaceRejected % (_surfaceLoadMicros / 1000.0) %
               tangents.meshes % tangents.vertices % (tangents.micros / 1000.0) % sample);
}

void Textures::applyAnisotropy() {
    float anisotropy = std::max(1.0f, exp2f(static_cast<float>(_options.anisotropicFiltering)));
    for (auto &[resRef, texture] : _cache) {
        if (texture) {
            texture->applyAnisotropy(anisotropy);
        }
    }
}

} // namespace resource

} // namespace reone
