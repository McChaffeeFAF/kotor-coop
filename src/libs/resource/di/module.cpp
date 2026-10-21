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

#include "reone/resource/di/module.h"

#include "reone/resource/extractresources.h"
#include "reone/resource/replacementresources.h"
#include "reone/resource/replacements.h"

#include "reone/audio/di/module.h"
#include "reone/graphics/di/module.h"
#include "reone/script/di/module.h"

namespace reone {

namespace resource {

void ResourceModule::init() {
    std::unique_ptr<IResources> backend;
    std::unique_ptr<IResources> auxBackend;
    if (_resourcesBackend == ResourcesBackend::Extract) {
        backend = std::make_unique<ExtractResources>();
        auxBackend = std::make_unique<ExtractResources>();
    } else {
        backend = std::make_unique<Resources>();
        auxBackend = std::make_unique<Resources>();
    }
    _replacements = std::make_unique<ResourceReplacements>();
    _resources = std::make_unique<ReplacementResources>(std::move(backend), *_replacements);
    if (!_k2DataPath.empty()) {
        // The K2 systems merge: K2's data set sits behind K1's resources for
        // the assets appended rows name (icons, effect models, sounds) and
        // behind the merged tables. K1 always wins a name both have.
        _k2DataResources = std::make_unique<Resources>();
        mountK2DataSources(*_k2DataResources, _k2DataPath);
        _resources = std::make_unique<K2FallbackResources>(std::move(_resources), *_k2DataResources);
    }
    // Auxiliary sources are read through the same replacement store, so a
    // replacement still sits above them. A second store would silently make
    // shader and cursor resources unreplaceable. The backend mirrors the
    // selected one because the two index folders differently, and streamed
    // audio lives here for an activated game.
    _auxResources = std::make_unique<ReplacementResources>(std::move(auxBackend), *_replacements);
    _strings = std::make_unique<Strings>();
    _twoDas = std::make_unique<TwoDAs>(*_resources);
    if (_k2DataResources) {
        _k2DataTwoDas = std::make_unique<TwoDAs>(*_k2DataResources);
        _mergedTwoDas = std::make_unique<K2MergedTwoDAs>(*_twoDas, *_k2DataTwoDas);
    }
    ITwoDAs &twoDaSet = _mergedTwoDas ? static_cast<ITwoDAs &>(*_mergedTwoDas) : *_twoDas;
    _gffs = std::make_unique<Gffs>(*_resources);
    _shaders = std::make_unique<Shaders>(_graphicsOpt, _graphics.shaderRegistry(), *_auxResources);
    _textures = std::make_unique<Textures>(_graphicsOpt, *_resources);
    _models = std::make_unique<Models>(*_textures, *_resources, _graphics.statistic());
    _walkmeshes = std::make_unique<Walkmeshes>(*_resources);
    _lips = std::make_unique<Lips>(*_resources);
    _fonts = std::make_unique<Fonts>(
        _graphics.context(),
        _graphics.meshRegistry(),
        _graphics.shaderRegistry(),
        _graphics.statistic(),
        *_textures,
        _graphics.uniforms());
    _cursors = std::make_unique<Cursors>(
        _graphics.context(),
        _graphics.meshRegistry(),
        _graphics.shaderRegistry(),
        *_textures,
        _graphics.uniforms(),
        _graphics.statistic(),
        *_auxResources);
    _audioClips = std::make_unique<AudioClips>(*_resources, *_auxResources);
    _movies = std::make_unique<Movies>(_gamePath, _graphics.services(), _audio.mixer());
    _scripts = std::make_unique<Scripts>(*_resources, *_replacements);
    _scripts->setK2Resources(_k2DataResources.get());
    _gffs->setK2Resources(_k2DataResources.get());
    _dialogs = std::make_unique<Dialogs>(*_gffs, *_strings);
    _layouts = std::make_unique<Layouts>(*_resources);
    _paths = std::make_unique<Paths>(*_gffs);
    _soundSets = std::make_unique<SoundSets>(*_audioClips, *_resources, *_strings);
    _visibilities = std::make_unique<Visibilities>(*_resources);
    _ltrs = std::make_unique<Ltrs>(*_resources);
    _director = std::make_unique<ResourceDirector>(
        _gameId,
        _gamePath,
        _graphicsOpt,
        _graphics.services(),
        _script.services(),
        *_dialogs,
        *_gffs,
        *_lips,
        *_paths,
        *_resources,
        *_auxResources,
        *_scripts,
        twoDaSet,
        _odysseyRoots);

    _strings->init(_gamePath);
    loadLiveTalkTables(*_strings, _odysseyRoots);
    if (_k2DataResources) {
        _strings->loadK2TalkTable(_k2DataPath);
        TalkTable::Builder own;
        for (const auto &text : k2MergeOwnStrings()) {
            own.string(text);
        }
        _strings->setOwnTalkTable(own.build());
    }
    _director->init();
    _shaders->init();
    _textures->init();
    // K2-only feat and power icons are K2-green; recolour them to K1 blue
    // unless the HUD is K2's.
    _textures->setK1IconPalette(_k2DataResources && _k2PresentationPath.empty());

    _services = std::make_unique<ResourceServices>(
        *_gffs,
        *_resources,
        *_replacements,
        *_strings,
        twoDaSet,
        *_scripts,
        *_movies,
        *_audioClips,
        *_cursors,
        *_fonts,
        *_lips,
        *_models,
        *_textures,
        *_walkmeshes,
        *_dialogs,
        *_layouts,
        *_paths,
        *_soundSets,
        *_visibilities,
        *_ltrs,
        *_shaders,
        *_director);
    _services->k2Merge = _mergedTwoDas.get();

    if (!_k2PresentationPath.empty()) {
        initK2Presentation();
    }
}

void ResourceModule::initK2Presentation() {
    auto backend = std::make_unique<Resources>();
    mountK2PresentationSources(*backend, _k2PresentationPath);
    // Harness replacements apply to the K2 set too, through the same store.
    _k2Resources = std::make_unique<LayeredPresentationResources>(
        std::make_unique<ReplacementResources>(std::move(backend), *_replacements),
        *_resources);
    _k2Gffs = std::make_unique<Gffs>(*_k2Resources);
    _k2Textures = std::make_unique<Textures>(_graphicsOpt, *_k2Resources);
    _k2Textures->init();
    _k2Fonts = std::make_unique<Fonts>(
        _graphics.context(),
        _graphics.meshRegistry(),
        _graphics.shaderRegistry(),
        _graphics.statistic(),
        *_k2Textures,
        _graphics.uniforms());
    // K2 GUI files carry K2 strrefs.
    _k2Strings = std::make_unique<Strings>();
    _k2Strings->init(_k2PresentationPath);

    _k2Services = std::make_unique<ResourceServices>(
        *_k2Gffs,
        *_k2Resources,
        *_replacements,
        *_k2Strings,
        _mergedTwoDas ? static_cast<ITwoDAs &>(*_mergedTwoDas) : *_twoDas,
        *_scripts,
        *_movies,
        *_audioClips,
        *_cursors,
        *_k2Fonts,
        *_lips,
        *_models,
        *_k2Textures,
        *_walkmeshes,
        *_dialogs,
        *_layouts,
        *_paths,
        *_soundSets,
        *_visibilities,
        *_ltrs,
        *_shaders,
        *_director);

    initK2Scene();
}

void ResourceModule::initK2Scene() {
    auto backend = std::make_unique<Resources>();
    try {
        mountK2SceneSources(*backend, _k2PresentationPath);
    } catch (const std::exception &) {
        // The menu falls back to K1's scene; the GUI set already validated
        // the install, so this is only a partial copy.
        return;
    }
    _k2SceneResources = std::make_unique<ReplacementResources>(std::move(backend), *_replacements);
    _k2SceneTextures = std::make_unique<Textures>(_graphicsOpt, *_k2SceneResources);
    _k2SceneTextures->init();
    _k2SceneModels = std::make_unique<Models>(*_k2SceneTextures, *_k2SceneResources, _graphics.statistic());
    _k2SceneTwoDas = std::make_unique<TwoDAs>(*_k2SceneResources);
    _k2SceneAudioClips = std::make_unique<AudioClips>(*_k2SceneResources, *_k2SceneResources);
    _k2SceneServices = std::make_unique<ResourceServices>(
        *_gffs,
        *_k2SceneResources,
        *_replacements,
        *_strings,
        *_k2SceneTwoDas,
        *_scripts,
        *_movies,
        *_k2SceneAudioClips,
        *_cursors,
        *_fonts,
        *_lips,
        *_k2SceneModels,
        *_k2SceneTextures,
        *_walkmeshes,
        *_dialogs,
        *_layouts,
        *_paths,
        *_soundSets,
        *_visibilities,
        *_ltrs,
        *_shaders,
        *_director);
}

void ResourceModule::deinit() {
    _k2SceneServices.reset();
    _k2SceneAudioClips.reset();
    _k2SceneTwoDas.reset();
    _k2SceneModels.reset();
    _k2SceneTextures.reset();
    _k2SceneResources.reset();

    _k2Services.reset();
    _k2Strings.reset();
    _k2Fonts.reset();
    _k2Textures.reset();
    _k2Gffs.reset();
    _k2Resources.reset();

    _services.reset();

    _director.reset();
    _ltrs.reset();
    _visibilities.reset();
    _soundSets.reset();
    _paths.reset();
    _layouts.reset();
    _dialogs.reset();
    _scripts.reset();
    _movies.reset();
    _audioClips.reset();
    _cursors.reset();
    _fonts.reset();
    _lips.reset();
    _walkmeshes.reset();
    _models.reset();
    _textures.reset();
    _shaders.reset();
    _gffs.reset();
    _mergedTwoDas.reset();
    _k2DataTwoDas.reset();
    _twoDas.reset();
    _strings.reset();
    _auxResources.reset();
    _resources.reset();
    _replacements.reset();
    _k2DataResources.reset();
}

} // namespace resource

} // namespace reone
