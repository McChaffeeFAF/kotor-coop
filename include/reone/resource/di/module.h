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

#include "../director.h"
#include "../provider/2das.h"
#include "../provider/audioclips.h"
#include "../provider/cursors.h"
#include "../provider/dialogs.h"
#include "../provider/fonts.h"
#include "../provider/gffs.h"
#include "../provider/layouts.h"
#include "../provider/lips.h"
#include "../provider/ltrs.h"
#include "../provider/models.h"
#include "../provider/movies.h"
#include "../provider/paths.h"
#include "../provider/scripts.h"
#include "../provider/shaders.h"
#include "../provider/soundsets.h"
#include "../provider/textures.h"
#include "../provider/visibilities.h"
#include "../provider/walkmeshes.h"
#include "../k2merge.h"
#include "../presentationresources.h"
#include "../replacements.h"
#include "../resources.h"
#include "../strings.h"

#include "services.h"

namespace reone {

namespace audio {

class AudioModule;

struct AudioOptions;

} // namespace audio

namespace graphics {

class GraphicsModule;

}

namespace script {

class ScriptModule;

}

namespace resource {

/// Backend that serves resource lookups. Extract reads game data through the
/// extract layer primitives, with lookup semantics identical to Legacy.
enum class ResourcesBackend {
    Legacy,
    Extract,
};

class ResourceModule : boost::noncopyable {
public:
    ResourceModule(GameID gameId,
                   std::filesystem::path gamePath,
                   graphics::GraphicsOptions &graphicsOpt,
                   audio::AudioOptions &audioOpt,
                   graphics::GraphicsModule &graphics,
                   audio::AudioModule &audio,
                   script::ScriptModule &script,
                   ResourcesBackend resourcesBackend = ResourcesBackend::Legacy,
                   OdysseyResourceRoots odysseyRoots = {}) :
        _gameId(gameId),
        _gamePath(std::move(gamePath)),
        _graphicsOpt(graphicsOpt),
        _audioOpt(audioOpt),
        _graphics(graphics),
        _audio(audio),
        _script(script),
        _resourcesBackend(resourcesBackend),
        _nwmFilesDerived(!odysseyRoots.nwmFiles),
        _odysseyRoots(std::move(odysseyRoots)) {
        if (!_odysseyRoots.nwmFiles) {
            _odysseyRoots.nwmFiles = defaultOdysseyResourceRoots(_gamePath).nwmFiles;
        }
    }

    ~ResourceModule() { deinit(); }

    void init();
    void deinit();

    Gffs &gffs() { return *_gffs; }
    IResources &resources() { return *_resources; }
    IResourceReplacements &replacements() { return *_replacements; }
    Strings &strings() { return *_strings; }
    ITwoDAs &twoDas() { return _mergedTwoDas ? static_cast<ITwoDAs &>(*_mergedTwoDas) : *_twoDas; }
    Scripts &scripts() { return *_scripts; }
    Movies &movies() { return *_movies; }
    AudioClips &audioClips() { return *_audioClips; }
    Cursors &cursors() { return *_cursors; }
    Fonts &fonts() { return *_fonts; }
    Lips &lips() { return *_lips; }
    Models &models() { return *_models; }
    Textures &textures() { return *_textures; }
    Walkmeshes &walkmeshes() { return *_walkmeshes; }
    Ltrs &ltrs() { return *_ltrs; }
    Shaders &shaders() { return *_shaders; }
    ResourceDirector &director() { return *_director; }

    ResourceServices &services() { return *_services; }

    /**
     * The K2 GUI resource set, when one is configured: a view over the main
     * services whose GUI, texture, font and string lookups read the K2 install
     * first and fall back to the main sources. Null otherwise.
     */
    ResourceServices *k2PresentationServices() { return _k2Services.get(); }

    /**
     * The K2 main-menu scene set, when a K2 install is configured: a view over
     * the main services whose model, texture, 2DA and music lookups read only
     * K2's models, world texture pack, 2DAs and stream music. Null otherwise.
     */
    ResourceServices *k2SceneServices() { return _k2SceneServices.get(); }

    /// The K2 systems merge, when a K2 data path is set. Null otherwise.
    K2MergedTwoDAs *k2Merge() { return _mergedTwoDas.get(); }

    /**
     * Merge K2's systems (feats, powers, classes, forms, the 50-level table,
     * crafting) into K1's data from this K2 install on init. Empty disables.
     */
    void setK2DataPath(std::filesystem::path path) {
        _k2DataPath = std::move(path);
    }

    /// Read the K2 GUI resource set from this install on init. Empty disables.
    void setK2PresentationPath(std::filesystem::path path) {
        _k2PresentationPath = std::move(path);
    }
    const OdysseyResourceRoots &odysseyRoots() const { return _odysseyRoots; }

    void setGameID(GameID id) {
        _gameId = id;
    }

    void setGamePath(std::filesystem::path path) {
        _gamePath = std::move(path);
        if (_nwmFilesDerived) {
            _odysseyRoots.nwmFiles = defaultOdysseyResourceRoots(_gamePath).nwmFiles;
        }
    }

private:
    GameID _gameId;
    std::filesystem::path _gamePath;
    graphics::GraphicsOptions &_graphicsOpt;
    audio::AudioOptions &_audioOpt;
    graphics::GraphicsModule &_graphics;
    audio::AudioModule &_audio;
    script::ScriptModule &_script;
    ResourcesBackend _resourcesBackend {ResourcesBackend::Legacy};

    bool _nwmFilesDerived {false};
    OdysseyResourceRoots _odysseyRoots;
    std::unique_ptr<Gffs> _gffs;
    std::unique_ptr<IResourceReplacements> _replacements;
    std::unique_ptr<IResources> _resources;
    /// Sources outside the Odyssey raw lookup model: the shader pack, the
    /// executable, and streamed audio for an activated game.
    std::unique_ptr<IResources> _auxResources;
    std::unique_ptr<Strings> _strings;
    std::unique_ptr<TwoDAs> _twoDas;
    std::unique_ptr<Scripts> _scripts;
    std::unique_ptr<Movies> _movies;
    std::unique_ptr<AudioClips> _audioClips;
    std::unique_ptr<Cursors> _cursors;
    std::unique_ptr<Fonts> _fonts;
    std::unique_ptr<Lips> _lips;
    std::unique_ptr<Models> _models;
    std::unique_ptr<Textures> _textures;
    std::unique_ptr<Walkmeshes> _walkmeshes;
    std::unique_ptr<Dialogs> _dialogs;
    std::unique_ptr<Layouts> _layouts;
    std::unique_ptr<Paths> _paths;
    std::unique_ptr<SoundSets> _soundSets;
    std::unique_ptr<Visibilities> _visibilities;
    std::unique_ptr<Ltrs> _ltrs;
    std::unique_ptr<Shaders> _shaders;
    std::unique_ptr<ResourceDirector> _director;

    std::unique_ptr<ResourceServices> _services;

    // K2 systems merge (data)

    std::filesystem::path _k2DataPath;
    std::unique_ptr<Resources> _k2DataResources;
    std::unique_ptr<TwoDAs> _k2DataTwoDas;
    std::unique_ptr<K2MergedTwoDAs> _mergedTwoDas;

    // END K2 systems merge (data)

    // K2 presentation set

    std::filesystem::path _k2PresentationPath;
    std::unique_ptr<LayeredPresentationResources> _k2Resources;
    std::unique_ptr<Gffs> _k2Gffs;
    std::unique_ptr<Textures> _k2Textures;
    std::unique_ptr<Fonts> _k2Fonts;
    std::unique_ptr<Strings> _k2Strings;
    std::unique_ptr<ResourceServices> _k2Services;

    std::unique_ptr<IResources> _k2SceneResources;
    std::unique_ptr<Textures> _k2SceneTextures;
    std::unique_ptr<Models> _k2SceneModels;
    std::unique_ptr<TwoDAs> _k2SceneTwoDas;
    std::unique_ptr<AudioClips> _k2SceneAudioClips;
    std::unique_ptr<ResourceServices> _k2SceneServices;

    // END K2 presentation set

    void initK2Presentation();
    void initK2Scene();
};

} // namespace resource

} // namespace reone
