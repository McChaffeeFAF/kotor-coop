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

#include "container.h"
#include "resources.h"

#include <set>

namespace reone {

namespace resource {

/// Resource types a presentation (GUI) resource set may serve.
const std::set<ResType> &presentationResourceTypes();

/**
 * Serves only the listed types from an inner container. Everything else in the
 * container is invisible, so mounting a whole KEY for its GUI entries cannot
 * leak its 2DAs, scripts or world textures.
 */
class TypeFilteredResourceContainer : public IResourceContainer, boost::noncopyable {
public:
    TypeFilteredResourceContainer(std::unique_ptr<IResourceContainer> inner,
                                  std::set<ResType> types);

    std::optional<ByteBuffer> findResourceData(const ResourceId &id) override;

    const std::unordered_set<ResourceId> &resourceIds() const override { return _resourceIds; }

private:
    std::unique_ptr<IResourceContainer> _inner;
    std::set<ResType> _types;
    std::unordered_set<ResourceId> _resourceIds;
};

/**
 * A presentation resource set layered over the main one.
 *
 * Lookups of presentation types consult the set first and fall back to the
 * main sources on a miss. Every other type goes straight to the main sources.
 * Mounts and retirements act on the set only; the main sources are borrowed and
 * never changed through this facade.
 */
class LayeredPresentationResources : public IResources, boost::noncopyable {
public:
    LayeredPresentationResources(std::unique_ptr<IResources> primary,
                                 IResources &fallback,
                                 std::set<ResType> types = presentationResourceTypes()) :
        _primary(std::move(primary)),
        _fallback(fallback),
        _types(std::move(types)) {
    }

    void clear() override { _primary->clear(); }
    void clearOwner(ResourceOwner owner) override { _primary->clearOwner(owner); }
    ResourceMountToken mountToken() const override { return _primary->mountToken(); }
    void rollbackTo(ResourceMountToken token) override { _primary->rollbackTo(token); }

    void addEXE(const std::filesystem::path &path,
                std::optional<ResourceSourceBucket> bucket = std::nullopt) override {
        _primary->addEXE(path, bucket);
    }
    void addKEY(const std::filesystem::path &path,
                std::optional<ResourceSourceBucket> bucket = std::nullopt) override {
        _primary->addKEY(path, bucket);
    }
    void addERF(const std::filesystem::path &path,
                ResourceOwner owner = ResourceOwner::Global,
                std::optional<ResourceSourceBucket> bucket = std::nullopt) override {
        _primary->addERF(path, owner, bucket);
    }
    void addMemERF(ByteBuffer buffer,
                   ResourceOwner owner,
                   std::optional<ResourceSourceBucket> bucket = std::nullopt) override {
        _primary->addMemERF(std::move(buffer), owner, bucket);
    }
    void addRIM(const std::filesystem::path &path,
                ResourceOwner owner = ResourceOwner::Global,
                std::optional<ResourceSourceBucket> bucket = std::nullopt) override {
        _primary->addRIM(path, owner, bucket);
    }
    void addMemRIM(ByteBuffer buffer,
                   ResourceOwner owner = ResourceOwner::Global,
                   std::optional<ResourceSourceBucket> bucket = std::nullopt) override {
        _primary->addMemRIM(std::move(buffer), owner, bucket);
    }
    void addFolder(const std::filesystem::path &path,
                   ResourceOwner owner = ResourceOwner::Global,
                   std::optional<ResourceSourceBucket> bucket = std::nullopt) override {
        _primary->addFolder(path, owner, bucket);
    }

    Resource get(const ResourceId &id) override;
    std::optional<Resource> find(const ResourceId &id) override;
    std::optional<Resource> findExcludingOwners(
        const ResourceId &id,
        const std::set<ResourceOwner> &excludedOwners) override;

    IResources &primary() { return *_primary; }

private:
    std::unique_ptr<IResources> _primary;
    IResources &_fallback;
    std::set<ResType> _types;
};

/// True when the directory looks like a K2 install (chitin.key and swkotor2.exe).
/**
 * Mount the K2 main-menu scene set: models, their textures, the 2DAs that
 * pick them, and K2's menu music, read from the player's K2 install.
 *
 * Presentation only. It is a standalone set with no fallback, so nothing in it
 * can answer a K1 lookup, and no K1 file can stand in for a K2 model.
 */
void mountK2SceneSources(Resources &set, const std::filesystem::path &k2Path);

bool isK2Install(const std::filesystem::path &path);

/**
 * Mount a K2 install's GUI resources into a set, in the precedence retail K2
 * uses for them: the loose override directory, then the GUI texture pack, then
 * the GUI entries of chitin.key. Only presentation types are visible, and only
 * GUI files from the KEY. World texture packs, RIMs, modules and LIPs are never
 * mounted.
 *
 * Throws std::runtime_error when the directory is not a K2 install or has no
 * GUI texture pack.
 */
void mountK2PresentationSources(Resources &set, const std::filesystem::path &k2Path);

} // namespace resource

} // namespace reone
