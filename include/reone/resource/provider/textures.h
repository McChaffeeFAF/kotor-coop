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

#pragma once

#include "reone/graphics/surfacedetail.h"
#include "reone/graphics/types.h"

namespace reone {

namespace graphics {

class GraphicsOptions;
class Texture;

} // namespace graphics

namespace resource {

class IResources;

class ITextures {
public:
    virtual ~ITextures() {
    }

    virtual void clear() = 0;

    virtual std::shared_ptr<graphics::Texture> get(const std::string &resRef, graphics::TextureUsage usage = graphics::TextureUsage::Default) = 0;

    /**
     * A feat, power or skill icon. With the K2 merge under a K1 HUD, an icon
     * only K2 has is hue-shifted (in memory) from K2's green UI palette to
     * K1's blue one; otherwise this is get().
     */
    virtual std::shared_ptr<graphics::Texture> getIcon(const std::string &resRef, graphics::TextureUsage usage = graphics::TextureUsage::GUI) {
        return get(resRef, usage);
    }

    /** Give every loaded texture the anisotropy the options now ask for. */
    virtual void applyAnisotropy() {}

    /**
     * Surface detail (Tier 4): the generation changes whenever the maps a
     * texture resolves to may have changed (the feature, the folder or the
     * test patterns switched), so a mesh re-resolves its maps only then.
     */
    struct SurfaceDetailState {
        int generation {0};
        bool enabled {false};
        float strength {1.0f};
    };

    virtual SurfaceDetailState surfaceDetail() { return SurfaceDetailState(); }

    /**
     * The generated normal or material map of a texture, from the override
     * folder (or a procedural test pattern), or null: the feature is off, the
     * file is missing or it does not load. Read only; nothing is written.
     */
    virtual std::shared_ptr<graphics::Texture> getSurfaceMap(const std::string &texture, graphics::surfacedetail::MapKind kind) { return nullptr; }

    /** Folder, maps found and missed, and the tangent cost, as one line of JSON. */
    virtual std::string surfaceDetailJson() { return "{}"; }
};

class Textures : public ITextures, boost::noncopyable {
public:
    Textures(graphics::GraphicsOptions &options, IResources &resources) :
        _options(options),
        _resources(resources) {
    }

    void init();

    void clear() override;

    std::shared_ptr<graphics::Texture> get(const std::string &resRef, graphics::TextureUsage usage = graphics::TextureUsage::Default) override;

    std::shared_ptr<graphics::Texture> getIcon(const std::string &resRef, graphics::TextureUsage usage = graphics::TextureUsage::GUI) override;

    /** On: icons only the K2 merge supplies are recoloured to K1's palette. */
    void setK1IconPalette(bool on) { _k1IconPalette = on; }

    void applyAnisotropy() override;

    SurfaceDetailState surfaceDetail() override;
    std::shared_ptr<graphics::Texture> getSurfaceMap(const std::string &texture, graphics::surfacedetail::MapKind kind) override;
    std::string surfaceDetailJson() override;

private:
    int _activeUnit {0};

    graphics::GraphicsOptions &_options;
    IResources &_resources;

    std::unordered_map<std::string, std::shared_ptr<graphics::Texture>> _cache;
    std::unordered_map<std::string, std::shared_ptr<graphics::Texture>> _iconCache;
    bool _k1IconPalette {false};

    std::shared_ptr<graphics::Texture> doGet(const std::string &resRef, graphics::TextureUsage usage);

    // Surface detail
    std::unordered_map<std::string, std::shared_ptr<graphics::Texture>> _surfaceCache;
    int _surfaceGeneration {0};
    bool _surfaceEnabled {false};
    bool _surfaceTest {false};
    std::string _surfaceFolder;
    int _surfaceFound {0};
    int _surfaceMissing {0};
    std::vector<std::string> _surfaceMissingSample; /**< the first few missing names, for the surfacedetail command */
    int _surfaceRejected {0};
    int64_t _surfaceLoadMicros {0};

    std::shared_ptr<graphics::Texture> loadSurfaceMap(const std::string &texture, graphics::surfacedetail::MapKind kind);
};

} // namespace resource

} // namespace reone
