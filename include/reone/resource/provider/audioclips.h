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

#include "../resource.h"
#include "../types.h"

#include <future>

namespace reone {

namespace audio {

class AudioClip;

}

namespace resource {

class IResources;

class IAudioClips {
public:
    virtual ~IAudioClips() = default;

    virtual void clear() = 0;

    virtual std::shared_ptr<audio::AudioClip> get(const std::string &key) = 0;

    /**
     * For music and ambience, which nothing in the simulation waits on: the
     * clip is decoded off the main thread. Returns it once decoded; while it
     * is still decoding returns nullptr and sets pending. Implementations
     * without a background path decode here.
     */
    virtual std::shared_ptr<audio::AudioClip> getInBackground(const std::string &key, bool &pending) {
        pending = false;
        return get(key);
    }
};

/**
 * Audio clips, read from the streamed audio directories first and from
 * ordinary resources second.
 *
 * The streaming directories are not part of the Odyssey raw lookup order, so
 * they are held separately and consulted explicitly rather than being given a
 * bucket they have no evidence for. Format preference is unchanged: MP3 is
 * still preferred over WAV across both, not within each.
 */
class AudioClips : public IAudioClips {
public:
    AudioClips(IResources &resources, IResources &streamResources) :
        _resources(resources),
        _streamResources(streamResources) {
    }

    void clear() override {
        _objects.clear();
    }

    std::shared_ptr<audio::AudioClip> get(const std::string &key) override;

    std::shared_ptr<audio::AudioClip> getInBackground(const std::string &key, bool &pending) override;

private:
    IResources &_resources;
    IResources &_streamResources;

    std::unordered_map<std::string, std::shared_ptr<audio::AudioClip>> _objects;
    std::unordered_map<std::string, std::future<std::shared_ptr<audio::AudioClip>>> _decoding;

    std::shared_ptr<audio::AudioClip> doGet(std::string resRef);
    std::optional<Resource> findClipData(const std::string &resRef, ResType type);
};

} // namespace resource

} // namespace reone
