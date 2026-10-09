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

#include "reone/resource/presentationresources.h"

#include "reone/resource/container/erf.h"
#include "reone/resource/container/folder.h"
#include "reone/resource/container/keybif.h"
#include "reone/resource/exception/notfound.h"
#include "reone/system/fileutil.h"

namespace reone {

namespace resource {

const std::set<ResType> &presentationResourceTypes() {
    static const std::set<ResType> kTypes {
        ResType::Gui, ResType::Tga, ResType::Tpc, ResType::Txi, ResType::Dds};
    return kTypes;
}

TypeFilteredResourceContainer::TypeFilteredResourceContainer(
    std::unique_ptr<IResourceContainer> inner,
    std::set<ResType> types) :
    _inner(std::move(inner)),
    _types(std::move(types)) {
    for (const auto &id : _inner->resourceIds()) {
        if (_types.count(id.type) != 0) {
            _resourceIds.insert(id);
        }
    }
}

std::optional<ByteBuffer> TypeFilteredResourceContainer::findResourceData(const ResourceId &id) {
    if (_types.count(id.type) == 0) {
        return std::nullopt;
    }
    return _inner->findResourceData(id);
}

Resource LayeredPresentationResources::get(const ResourceId &id) {
    auto found = find(id);
    if (!found) {
        throw ResourceNotFoundException(id.string());
    }
    return *found;
}

std::optional<Resource> LayeredPresentationResources::find(const ResourceId &id) {
    if (_types.count(id.type) != 0) {
        if (auto found = _primary->find(id)) {
            return found;
        }
    }
    return _fallback.find(id);
}

std::optional<Resource> LayeredPresentationResources::findExcludingOwners(
    const ResourceId &id,
    const std::set<ResourceOwner> &excludedOwners) {
    if (_types.count(id.type) != 0) {
        if (auto found = _primary->findExcludingOwners(id, excludedOwners)) {
            return found;
        }
    }
    return _fallback.findExcludingOwners(id, excludedOwners);
}

bool isK2Install(const std::filesystem::path &path) {
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_directory(path, ec)) {
        return false;
    }
    return findFileIgnoreCase(path, "chitin.key").has_value() &&
           findFileIgnoreCase(path, "swkotor2.exe").has_value();
}

void mountK2PresentationSources(Resources &set, const std::filesystem::path &k2Path) {
    if (!isK2Install(k2Path)) {
        throw std::runtime_error(
            "Not a K2 install (chitin.key and swkotor2.exe expected): " + k2Path.string());
    }
    auto pack = findFileIgnoreCase(k2Path, "texturepacks/swpc_tex_gui.erf");
    if (!pack) {
        throw std::runtime_error("K2 GUI texture pack not found under " + k2Path.string());
    }
    const auto &types = presentationResourceTypes();

    // Mounted in the order the buckets rank them from the bottom up; bucket
    // placement, not this order, decides the winner.
    auto keyPath = findFileIgnoreCase(k2Path, "chitin.key");
    auto key = std::make_unique<KeyBifResourceContainer>(*keyPath);
    key->init();
    set.add(std::make_unique<TypeFilteredResourceContainer>(std::move(key), std::set<ResType> {ResType::Gui}),
            ResourceOwner::Global,
            ResourceSourceBucket::KeyBif);

    auto erf = std::make_unique<ErfResourceContainer>(*pack);
    erf->init();
    set.add(std::make_unique<TypeFilteredResourceContainer>(std::move(erf), types),
            ResourceOwner::Global,
            ResourceSourceBucket::EncapsulatedClass2);

    if (auto overridePath = findFileIgnoreCase(k2Path, "override")) {
        auto folder = std::make_unique<FolderResourceContainer>(*overridePath);
        folder->init();
        set.add(std::make_unique<TypeFilteredResourceContainer>(std::move(folder), types),
                ResourceOwner::Global,
                ResourceSourceBucket::LooseDirectory);
    }
}

void mountK2SceneSources(Resources &set, const std::filesystem::path &k2Path) {
    if (!isK2Install(k2Path)) {
        throw std::runtime_error(
            "Not a K2 install (chitin.key and swkotor2.exe expected): " + k2Path.string());
    }
    static const std::set<ResType> kModelTypes {ResType::Mdl, ResType::Mdx, ResType::TwoDA};
    static const std::set<ResType> kTextureTypes {ResType::Tga, ResType::Tpc, ResType::Txi, ResType::Dds};
    static const std::set<ResType> kLooseTypes {
        ResType::Mdl, ResType::Mdx, ResType::TwoDA, ResType::Tga, ResType::Tpc, ResType::Txi, ResType::Dds};
    static const std::set<ResType> kMusicTypes {ResType::Wav, ResType::Mp3};

    auto keyPath = findFileIgnoreCase(k2Path, "chitin.key");
    auto key = std::make_unique<KeyBifResourceContainer>(*keyPath);
    key->init();
    set.add(std::make_unique<TypeFilteredResourceContainer>(std::move(key), kModelTypes),
            ResourceOwner::Global,
            ResourceSourceBucket::KeyBif);

    // Character and stage textures live in the world pack; the menu stage
    // also draws a few from the GUI pack, which ranks below it.
    for (const char *name : {"texturepacks/swpc_tex_gui.erf", "texturepacks/swpc_tex_tpa.erf"}) {
        if (auto pack = findFileIgnoreCase(k2Path, name)) {
            auto erf = std::make_unique<ErfResourceContainer>(*pack);
            erf->init();
            set.add(std::make_unique<TypeFilteredResourceContainer>(std::move(erf), kTextureTypes),
                    ResourceOwner::Global,
                    ResourceSourceBucket::EncapsulatedClass2);
        }
    }

    if (auto musicPath = findFileIgnoreCase(k2Path, "streammusic")) {
        auto folder = std::make_unique<FolderResourceContainer>(*musicPath);
        folder->init();
        set.add(std::make_unique<TypeFilteredResourceContainer>(std::move(folder), kMusicTypes),
                ResourceOwner::Global,
                ResourceSourceBucket::LooseDirectory);
    }

    if (auto overridePath = findFileIgnoreCase(k2Path, "override")) {
        auto folder = std::make_unique<FolderResourceContainer>(*overridePath);
        folder->init();
        set.add(std::make_unique<TypeFilteredResourceContainer>(std::move(folder), kLooseTypes),
                ResourceOwner::Global,
                ResourceSourceBucket::LooseDirectory);
    }
}

} // namespace resource

} // namespace reone
