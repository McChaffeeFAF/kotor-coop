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

#include "reone/resource/provider/audioclips.h"

#include "reone/audio/clip.h"
#include "reone/audio/format/mp3reader.h"
#include "reone/audio/format/wavreader.h"
#include "reone/resource/resources.h"
#include "reone/system/frameperf.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone::audio;

namespace reone {

namespace resource {

std::optional<Resource> AudioClips::findClipData(const std::string &resRef, ResType type) {
    auto id = ResourceId(resRef, type);
    // The streaming directories answer for a streamed asset before the ordinary
    // sources do. The traced engine reaches them by path rather than through
    // raw lookup, so the streaming location is authoritative for what it holds,
    // and retail relies on it: a K2 installation ships hundreds of voice lines
    // in both StreamVoice and the key tables, and the streamed copy is the one
    // that must play.
    //
    // This is the streaming subsystem's own rule, not a bucket. It does mean an
    // ordinary source cannot shadow a streamed asset of the same name, which
    // should be revisited if audio moves onto a path provider.
    auto res = _streamResources.find(id);
    if (res) {
        return res;
    }
    return _resources.find(id);
}

namespace {

std::shared_ptr<AudioClip> decodeMp3(ByteBuffer &data) {
    auto stream = MemoryInputStream(data);
    auto reader = Mp3Reader();
    reader.load(stream);
    return reader.stream();
}

std::shared_ptr<AudioClip> decodeWav(ByteBuffer &data) {
    auto stream = MemoryInputStream(data);
    auto mp3ReaderFactory = Mp3ReaderFactory();
    auto reader = WavReader(stream, mp3ReaderFactory);
    reader.load();
    return reader.stream();
}

} // namespace

std::shared_ptr<AudioClip> AudioClips::get(const std::string &key) {
    auto maybeObject = _objects.find(key);
    if (maybeObject != _objects.end()) {
        return maybeObject->second;
    }
    std::shared_ptr<AudioClip> object;
    auto decoding = _decoding.find(key);
    if (decoding != _decoding.end()) {
        // Asked for synchronously while a background decode runs: wait for it.
        object = decoding->second.get();
        _decoding.erase(decoding);
    } else {
        object = doGet(key);
    }
    return _objects.insert(make_pair(key, std::move(object))).first->second;
}

std::shared_ptr<AudioClip> AudioClips::getInBackground(const std::string &key, bool &pending) {
    pending = false;
    auto maybeObject = _objects.find(key);
    if (maybeObject != _objects.end()) {
        return maybeObject->second;
    }
    auto decoding = _decoding.find(key);
    if (decoding == _decoding.end()) {
        // The archive read stays here (the resource containers are not
        // thread-safe); only the decode, a whole music track through libmad
        // (hundreds of milliseconds), moves to a worker.
        std::future<std::shared_ptr<AudioClip>> future;
        if (auto mp3 = findClipData(key, ResType::Mp3)) {
            future = std::async(std::launch::async, [data = std::move(mp3->data)]() mutable {
                return decodeMp3(data);
            });
        } else if (auto wav = findClipData(key, ResType::Wav)) {
            future = std::async(std::launch::async, [data = std::move(wav->data)]() mutable {
                return decodeWav(data);
            });
        } else {
            return _objects.insert(make_pair(key, nullptr)).first->second;
        }
        decoding = _decoding.emplace(key, std::move(future)).first;
    }
    if (decoding->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        pending = true;
        return nullptr;
    }
    auto clip = decoding->second.get();
    _decoding.erase(decoding);
    return _objects.insert(make_pair(key, std::move(clip))).first->second;
}

std::shared_ptr<AudioClip> AudioClips::doGet(std::string resRef) {
    frameperf::Scope perfScope(frameperf::Phase::Resource, resRef);
    std::shared_ptr<AudioClip> clip;
    auto m3pRes = findClipData(resRef, ResType::Mp3);
    if (m3pRes) {
        clip = decodeMp3(m3pRes->data);
    }
    if (!clip) {
        auto wavRes = findClipData(resRef, ResType::Wav);
        if (wavRes) {
            clip = decodeWav(wavRes->data);
        }
    }
    return clip;
}

} // namespace resource

} // namespace reone
