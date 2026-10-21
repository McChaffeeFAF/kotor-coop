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

#include <algorithm>
#include <chrono>
#include <cmath>

#include <boost/algorithm/string.hpp>

#include "reone/graphics/format/curreader.h"
#include "reone/graphics/format/ddsreader.h"
#include "reone/graphics/format/tgareader.h"
#include "reone/graphics/format/tpcreader.h"
#include "reone/graphics/format/txireader.h"
#include "reone/graphics/options.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/textureutil.h"
#include "reone/graphics/types.h"
#include "reone/resource/k2merge.h"
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
    _iconCache.clear();
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

// K2's UI palette is green and K1's blue; icons from K2 carry the green.
// Hues from green to cyan move to K1's blue, with soft edges, so reds, greys
// and whites stay as they are.
static void shiftGreenToBlue(uint8_t &r, uint8_t &g, uint8_t &b) {
    float fr = r / 255.0f, fg = g / 255.0f, fb = b / 255.0f;
    float mx = std::max({fr, fg, fb}), mn = std::min({fr, fg, fb});
    float d = mx - mn;
    if (d < 1e-4f || mx < 1e-4f) {
        return;
    }
    float s = d / mx;
    float h;
    if (mx == fr) {
        h = std::fmod((fg - fb) / d, 6.0f);
    } else if (mx == fg) {
        h = (fb - fr) / d + 2.0f;
    } else {
        h = (fr - fg) / d + 4.0f;
    }
    h *= 60.0f;
    if (h < 0.0f) {
        h += 360.0f;
    }
    // Green and teal map to K1's icon blue (about 225 degrees), the span
    // compressed so teal does not overshoot into purple.
    float target = 225.0f + (std::clamp(h, 90.0f, 200.0f) - 160.0f) * 0.35f;
    float w = std::clamp((h - 60.0f) / 30.0f, 0.0f, 1.0f) * std::clamp((215.0f - h) / 15.0f, 0.0f, 1.0f);
    w *= std::clamp(s * 4.0f, 0.0f, 1.0f);
    if (w <= 0.0f) {
        return;
    }
    h = std::fmod(h + (target - h) * w + 360.0f, 360.0f);
    float hp = h / 60.0f;
    float c = mx * s;
    float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
    float m = mx - c;
    float or_ = 0, og = 0, ob = 0;
    int sector = static_cast<int>(hp) % 6;
    switch (sector) {
    case 0: or_ = c; og = x; break;
    case 1: or_ = x; og = c; break;
    case 2: og = c; ob = x; break;
    case 3: og = x; ob = c; break;
    case 4: or_ = x; ob = c; break;
    default: or_ = c; ob = x; break;
    }
    r = static_cast<uint8_t>(std::lround(std::clamp(or_ + m, 0.0f, 1.0f) * 255.0f));
    g = static_cast<uint8_t>(std::lround(std::clamp(og + m, 0.0f, 1.0f) * 255.0f));
    b = static_cast<uint8_t>(std::lround(std::clamp(ob + m, 0.0f, 1.0f) * 255.0f));
}

// Decode a DXT1 or DXT5 level into BGRA8.
static ByteBuffer decodeDXT(const ByteBuffer &blocks, int width, int height, bool dxt5) {
    ByteBuffer out(static_cast<size_t>(width) * height * 4, 0);
    int bw = (width + 3) / 4, bh = (height + 3) / 4;
    size_t blockSize = dxt5 ? 16 : 8;
    auto rgb565 = [](uint16_t c, uint8_t *rgb) {
        rgb[0] = static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31);
        rgb[1] = static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63);
        rgb[2] = static_cast<uint8_t>((c & 31) * 255 / 31);
    };
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            size_t offset = (static_cast<size_t>(by) * bw + bx) * blockSize;
            if (offset + blockSize > blocks.size()) {
                return out;
            }
            auto *b = reinterpret_cast<const uint8_t *>(blocks.data()) + offset;
            uint8_t alpha[8] {};
            uint64_t alphaBits = 0;
            if (dxt5) {
                alpha[0] = b[0];
                alpha[1] = b[1];
                for (int i = 0; i < 6; ++i) {
                    alphaBits |= static_cast<uint64_t>(b[2 + i]) << (8 * i);
                }
                if (alpha[0] > alpha[1]) {
                    for (int i = 1; i < 7; ++i) {
                        alpha[1 + i] = static_cast<uint8_t>(((7 - i) * alpha[0] + i * alpha[1]) / 7);
                    }
                } else {
                    for (int i = 1; i < 5; ++i) {
                        alpha[1 + i] = static_cast<uint8_t>(((5 - i) * alpha[0] + i * alpha[1]) / 5);
                    }
                    alpha[6] = 0;
                    alpha[7] = 255;
                }
                b += 8;
            }
            uint16_t c0 = static_cast<uint16_t>(b[0] | (b[1] << 8));
            uint16_t c1 = static_cast<uint16_t>(b[2] | (b[3] << 8));
            uint8_t col[4][4];
            rgb565(c0, col[0]);
            rgb565(c1, col[1]);
            col[0][3] = col[1][3] = col[2][3] = 255;
            col[3][3] = 255;
            bool fourColors = dxt5 || c0 > c1;
            for (int k = 0; k < 3; ++k) {
                if (fourColors) {
                    col[2][k] = static_cast<uint8_t>((2 * col[0][k] + col[1][k]) / 3);
                    col[3][k] = static_cast<uint8_t>((col[0][k] + 2 * col[1][k]) / 3);
                } else {
                    col[2][k] = static_cast<uint8_t>((col[0][k] + col[1][k]) / 2);
                    col[3][k] = 0;
                }
            }
            if (!fourColors) {
                col[3][3] = 0;
            }
            uint32_t bits = b[4] | (b[5] << 8) | (b[6] << 16) | (static_cast<uint32_t>(b[7]) << 24);
            for (int py = 0; py < 4; ++py) {
                for (int px = 0; px < 4; ++px) {
                    int x = bx * 4 + px, y = by * 4 + py;
                    if (x >= width || y >= height) {
                        continue;
                    }
                    int idx = (bits >> (2 * (py * 4 + px))) & 3;
                    uint8_t *o = reinterpret_cast<uint8_t *>(out.data()) + (static_cast<size_t>(y) * width + x) * 4;
                    o[0] = col[idx][2];
                    o[1] = col[idx][1];
                    o[2] = col[idx][0];
                    o[3] = dxt5 ? alpha[(alphaBits >> (3 * (py * 4 + px))) & 7] : col[idx][3];
                }
            }
        }
    }
    return out;
}

std::shared_ptr<Texture> Textures::getIcon(const std::string &resRef, TextureUsage usage) {
    auto base = get(resRef, usage);
    if (!base || !_k1IconPalette) {
        return base;
    }
    auto *fallback = dynamic_cast<K2FallbackResources *>(&_resources);
    if (!fallback) {
        return base;
    }
    std::string key(boost::to_lower_copy(resRef));
    if (auto cached = _iconCache.find(key); cached != _iconCache.end()) {
        return cached->second;
    }
    std::shared_ptr<Texture> result = base;
    bool k1Has = fallback->primary().find(ResourceId(key, ResType::Tga)) ||
                 fallback->primary().find(ResourceId(key, ResType::Tpc));
    if (!k1Has) {
        PixelFormat format = base->pixelFormat();
        std::vector<Texture::Layer> decoded;
        if ((format == PixelFormat::DXT1 || format == PixelFormat::DXT5) && base->is2D() && !base->layers().empty() && base->layers()[0].pixels) {
            decoded.push_back(Texture::Layer {std::make_shared<ByteBuffer>(decodeDXT(*base->layers()[0].pixels, base->width(), base->height(), format == PixelFormat::DXT5))});
            format = PixelFormat::BGRA8;
        }
        const auto &sourceLayers = decoded.empty() ? base->layers() : decoded;
        bool bgr = format == PixelFormat::BGR8 || format == PixelFormat::BGRA8;
        bool rgb = format == PixelFormat::RGB8 || format == PixelFormat::RGBA8;
        if ((bgr || rgb) && base->is2D() && !sourceLayers.empty()) {
            int stride = (format == PixelFormat::BGRA8 || format == PixelFormat::RGBA8) ? 4 : 3;
            std::vector<Texture::Layer> layers;
            for (const auto &layer : sourceLayers) {
                auto pixels = std::make_shared<ByteBuffer>(*layer.pixels);
                for (size_t i = 0; i + stride <= pixels->size(); i += stride) {
                    auto *p = reinterpret_cast<uint8_t *>(pixels->data() + i);
                    if (bgr) {
                        shiftGreenToBlue(p[2], p[1], p[0]);
                    } else {
                        shiftGreenToBlue(p[0], p[1], p[2]);
                    }
                }
                layers.push_back(Texture::Layer {std::move(pixels)});
            }
            auto copy = std::make_shared<Texture>(base->name(), base->type(), getTextureProperties(usage));
            copy->setFeatures(base->features());
            copy->setPixels(base->width(), base->height(), format, std::move(layers));
            copy->init();
            result = std::move(copy);
        } else {
            warn("K2 icon not recoloured (pixel format " + std::to_string(static_cast<int>(format)) + "): " + resRef, LogChannel::Graphics);
        }
    }
    _iconCache[key] = result;
    return result;
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
                    features->alphaTest = texture->features().alphaTest; // a TPC header value, not TXI
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
            if (path.extension() == ".dds") {
                auto reader = DdsReader(stream, name, TextureUsage::BumpMap, Texture::supportsBC7());
                reader.load();
                map = reader.texture();
            } else {
                auto reader = TgaReader(stream, name, TextureUsage::BumpMap);
                reader.load();
                map = reader.texture();
            }
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
