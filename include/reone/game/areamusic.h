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

#include <string>
#include <vector>

#include <string>

namespace reone {

namespace game {

/**
 * An area's music and ambient controller (GIT AreaProperties).
 *
 * Tracks are rows of ambientmusic.2da (music) and ambientsound.2da (ambient);
 * the area resolves them to resrefs. The switches are simulation state,
 * changed by script (MusicBackground*, MusicBattle*, AmbientSound*) and by
 * combat; the game's music and ambient players follow current() and
 * currentAmbient() per frame.
 *
 * K1 has no day/night cycle: the night slots are only heard in an area
 * flagged IsNight, which no K1 area is.
 */
class AreaMusic {
public:
    struct Track {
        int row {0};
        std::string resRef;
        // ambientmusic.2da stinger1-3: the non-empty ones, from stinger1 on.
        std::vector<std::string> stingers;
    };

    /** Test/legacy shorthand: the day background and the battle track. */
    void setTracks(std::string background, std::string battle) {
        _day.resRef = std::move(background);
        _battle.resRef = std::move(battle);
    }

    void setDay(Track track) { _day = std::move(track); }
    void setNight(Track track) { _night = std::move(track); }
    void setBattle(Track track) { _battle = std::move(track); }
    void setAmbientDay(Track track) { _ambientDay = std::move(track); }
    void setAmbientNight(Track track) { _ambientNight = std::move(track); }
    void setDelay(int delayMs) { _delayMs = delayMs; }
    void setNightArea(bool night) { _nightArea = night; }

    /** Ambient volumes are 0-100; anything else is ignored. */
    void setAmbientDayVolume(int volume) {
        if (volume >= 0 && volume <= 100) {
            _ambientDayVolume = volume;
        }
    }
    void setAmbientNightVolume(int volume) {
        if (volume >= 0 && volume <= 100) {
            _ambientNightVolume = volume;
        }
    }

    void setBackgroundPlaying(bool playing) { _backgroundPlaying = playing; }

    /**
     * Stopping battle music brings the background back, even if a script
     * had stopped it: the battle track replaced it, and ending the battle
     * hands the area back to its background music.
     */
    void setBattlePlaying(bool playing) {
        if (_battlePlaying && !playing) {
            _backgroundPlaying = true;
        }
        _battlePlaying = playing;
    }

    void setAmbientPlaying(bool playing) { _ambientPlaying = playing; }

    bool isBackgroundPlaying() const { return _backgroundPlaying; }
    bool isBattlePlaying() const { return _battlePlaying; }
    bool isAmbientPlaying() const { return _ambientPlaying; }
    bool isNightArea() const { return _nightArea; }

    const Track &day() const { return _day; }
    const Track &night() const { return _night; }
    const Track &battleTrack() const { return _battle; }
    const Track &ambientDay() const { return _ambientDay; }
    const Track &ambientNight() const { return _ambientNight; }
    int delay() const { return _delayMs; }
    int ambientDayVolume() const { return _ambientDayVolume; }
    int ambientNightVolume() const { return _ambientNightVolume; }

    const std::string &background() const { return _nightArea ? _night.resRef : _day.resRef; }
    const std::string &battle() const { return _battle.resRef; }

    /** The track that should be heard now, or empty for silence. */
    std::string current() const {
        if (_battlePlaying && !_battle.resRef.empty()) {
            return _battle.resRef;
        }
        if (_backgroundPlaying) {
            return background();
        }
        return std::string();
    }

    /** The ambient bed that should be heard now, or empty for silence. */
    std::string currentAmbient() const {
        if (!_ambientPlaying) {
            return std::string();
        }
        return _nightArea ? _ambientNight.resRef : _ambientDay.resRef;
    }

    /** Volume of the ambient bed, 0-100. */
    int currentAmbientVolume() const {
        return _nightArea ? _ambientNightVolume : _ambientDayVolume;
    }

private:
    Track _day;
    Track _night;
    Track _battle;
    Track _ambientDay;
    Track _ambientNight;
    int _delayMs {5000};
    int _ambientDayVolume {0};
    int _ambientNightVolume {0};
    bool _nightArea {false};
    bool _backgroundPlaying {true};
    bool _battlePlaying {false};
    bool _ambientPlaying {true};
};

} // namespace game

} // namespace reone
