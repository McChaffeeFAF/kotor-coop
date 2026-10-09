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

// Campaign batch C04: audio, music and cinematic camera routines.
// Behaviour notes: design note c04_audio.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/scene.h"

#include "reone/audio/clip.h"
#include "reone/game/areamusic.h"
#include "reone/scene/node/sound.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/object/sound.h"
#include "reone/game/party.h"
#include "reone/game/saveprovenance.h"
#include "reone/game/script/routines.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"
#include "reone/system/randomutil.h"

using namespace reone;
using namespace reone::audio;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

std::shared_ptr<TwoDA> makeTable(
    const std::vector<std::string> &columns,
    const std::vector<std::vector<std::string>> &rows) {
    TwoDA::Builder builder;
    builder.columns(columns);
    for (const auto &row : rows) {
        builder.row(row);
    }
    return std::shared_ptr<TwoDA>(builder.build());
}

/**
 * A private engine (its mocks take expectations), a game and the routine
 * table, with an area installed as the current module area.
 */
class C04Test : public Test {
protected:
    void SetUp() override {
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _routines->init();
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(_sceneGraph));
        ON_CALL(_sceneGraph, newSound()).WillByDefault(Invoke([this]() {
            return std::make_shared<scene::SoundSceneNode>(
                _sceneGraph, _engine.services().graphics, _engine.services().audio, _engine.services().resource);
        }));
        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);

        auto &twoDas = _engine.resourceModule().twoDas();
        ON_CALL(twoDas, get("ambientmusic")).WillByDefault(Return(makeTable(
            {"description", "resource"},
            {{"0", ""}, {"1", "mus_area_one"}, {"2", "mus_area_two"}, {"3", "mus_bat_three"}})));
        ON_CALL(twoDas, get("ambientsound")).WillByDefault(Return(makeTable(
            {"description", "resource"},
            {{"0", "al_en_zero"}, {"1", "al_en_one"}})));
        ON_CALL(twoDas, get("excitedduration")).WillByDefault(Return(makeTable(
            {"label", "duration"},
            {{"None", "0"}, {"Damage", "10000"}, {"SpellCast", "10000"}})));
    }

    Variable call(const std::string &name, std::vector<Variable> args, std::shared_ptr<Object> caller = nullptr) {
        Routine &routine = _routines->get(_routines->getIndexByName(name));
        ExecutionContext execution;
        execution.routines = _routines.get();
        if (caller) {
            execution.args.push_back(Argument(ArgKind::Caller, Variable::ofObject(caller->id())));
        }
        return routine.invoke(args, execution);
    }

    Variable areaArg() { return Variable::ofObject(_area->id()); }

    void hostile(Faction source, Faction target) {
        auto &reputes = static_cast<MockReputes &>(_engine.services().game.reputes);
        EXPECT_CALL(reputes, getIsEnemy(An<const Creature &>(), An<const Creature &>()))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([source, target](const Creature &from, const Creature &to) {
                return (from.faction() == source && to.faction() == target) ||
                       (from.faction() == target && to.faction() == source);
            }));
    }

    std::shared_ptr<Creature> addCreature(Faction faction, glm::vec3 position) {
        auto creature = _game->newCreature();
        creature->setFaction(faction);
        creature->setPosition(position);
        creature->setMaxHitPoints(100);
        creature->setCurrentHitPoints(100);
        _area->add(creature);
        return creature;
    }

    std::shared_ptr<Creature> addLeader() {
        auto leader = addCreature(Faction::Friendly1, glm::vec3(0.0f));
        _game->party().addMember(kNpcPlayer, leader);
        _game->party().setPlayer(leader);
        return leader;
    }

    std::shared_ptr<Sound> newSound(float pitchVariation, int volumeVariation) {
        auto sounds = Gff::Builder().type(0).field(Gff::Field::newResRef("Sound", "as_test")).build();
        auto gff = Gff::Builder().type(6)
                       .field(Gff::Field::newCExoString("Tag", "wind"))
                       .field(Gff::Field::newByte("Active", 1))
                       .field(Gff::Field::newByte("Volume", 100))
                       .field(Gff::Field::newByte("VolumeVrtn", static_cast<uint32_t>(volumeVariation)))
                       .field(Gff::Field::newFloat("PitchVariation", pitchVariation))
                       .field(Gff::Field::newFloat("MaxDistance", 20.0f))
                       .field(Gff::Field::newList("Sounds", {sounds}))
                       .build();
        auto sound = _game->newSound(*gff, SerializedIdentityContext::templateResource("test"));
        _area->add(sound);
        return sound;
    }

    TestEngine _engine;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
    std::shared_ptr<Area> _area;
};

} // namespace

// Area music controller.

TEST(C04AreaMusic, stopping_battle_music_brings_the_background_back) {
    AreaMusic music;
    music.setTracks("mus_area_endar", "mus_bat_endar");
    music.setBackgroundPlaying(false);
    music.setBattlePlaying(true);
    EXPECT_EQ("mus_bat_endar", music.current());
    music.setBattlePlaying(false);
    EXPECT_TRUE(music.isBackgroundPlaying());
    EXPECT_EQ("mus_area_endar", music.current());
    // Stopping battle music that was not playing changes nothing.
    music.setBackgroundPlaying(false);
    music.setBattlePlaying(false);
    EXPECT_EQ("", music.current());
}

TEST(C04AreaMusic, day_tracks_play_unless_the_area_is_night) {
    AreaMusic music;
    music.setDay({1, "mus_day"});
    music.setNight({2, "mus_night"});
    music.setAmbientDay({3, "al_day"});
    music.setAmbientNight({4, "al_night"});
    music.setAmbientDayVolume(80);
    music.setAmbientNightVolume(20);
    EXPECT_EQ("mus_day", music.current());
    EXPECT_EQ("al_day", music.currentAmbient());
    EXPECT_EQ(80, music.currentAmbientVolume());
    music.setNightArea(true);
    EXPECT_EQ("mus_night", music.current());
    EXPECT_EQ("al_night", music.currentAmbient());
    EXPECT_EQ(20, music.currentAmbientVolume());
}

TEST(C04AreaMusic, ambient_volume_outside_0_to_100_is_ignored) {
    AreaMusic music;
    music.setAmbientDayVolume(50);
    music.setAmbientDayVolume(101);
    music.setAmbientDayVolume(-1);
    EXPECT_EQ(50, music.ambientDayVolume());
}

// MusicBackgroundChangeDay / Night, getters, SetMusicVolume.

TEST_F(C04Test, music_background_change_day_switches_the_track_and_starts_it) {
    call("MusicBackgroundStop", {areaArg()});
    EXPECT_EQ("", _area->music());
    call("MusicBackgroundChangeDay", {areaArg(), Variable::ofInt(2)});
    EXPECT_EQ("mus_area_two", _area->music());
    EXPECT_TRUE(_area->musicState().isBackgroundPlaying());
    EXPECT_EQ(2, call("MusicBackgroundGetDayTrack", {areaArg()}).intValue);
    EXPECT_EQ("mus_area_two", _game->musicResRef());
}

TEST_F(C04Test, music_background_change_night_is_stored_and_not_heard_in_a_day_area) {
    call("MusicBackgroundChangeDay", {areaArg(), Variable::ofInt(1)});
    call("MusicBackgroundChangeNight", {areaArg(), Variable::ofInt(2)});
    EXPECT_EQ(2, call("MusicBackgroundGetNightTrack", {areaArg()}).intValue);
    EXPECT_EQ("mus_area_one", _area->music());
    call("MusicBattleChange", {areaArg(), Variable::ofInt(3)});
    EXPECT_EQ(3, call("MusicBackgroundGetBattleTrack", {areaArg()}).intValue);
}

TEST_F(C04Test, music_routines_ignore_objects_that_are_not_areas) {
    auto creature = _game->newCreature();
    call("MusicBackgroundChangeDay", {Variable::ofObject(creature->id()), Variable::ofInt(2)});
    EXPECT_EQ(0, call("MusicBackgroundGetDayTrack", {Variable::ofObject(creature->id())}).intValue);
}

TEST_F(C04Test, set_music_volume_clamps_to_0_1) {
    call("SetMusicVolume", {Variable::ofFloat(0.25f)});
    EXPECT_FLOAT_EQ(0.25f, _game->musicVolume());
    call("SetMusicVolume", {Variable::ofFloat(3.0f)});
    EXPECT_FLOAT_EQ(1.0f, _game->musicVolume());
    call("SetMusicVolume", {Variable::ofFloat(-1.0f)});
    EXPECT_FLOAT_EQ(0.0f, _game->musicVolume());
}

// AmbientSoundPlay / Stop and the ambient setters.

TEST_F(C04Test, ambient_sound_stop_and_play_switch_the_ambient_bed) {
    _area->musicState().setAmbientDay(_area->ambientTrack(1));
    _area->musicState().setAmbientDayVolume(60);
    EXPECT_EQ("al_en_one", _area->musicState().currentAmbient());

    call("AmbientSoundStop", {areaArg()});
    EXPECT_EQ("", _area->musicState().currentAmbient());
    EXPECT_EQ("", _game->ambientResRef());

    call("AmbientSoundPlay", {areaArg()});
    EXPECT_EQ("al_en_one", _game->ambientResRef());

    call("AmbientSoundChangeDay", {areaArg(), Variable::ofInt(0)});
    EXPECT_EQ("al_en_zero", _game->ambientResRef());
    call("AmbientSoundSetDayVolume", {areaArg(), Variable::ofInt(150)});
    EXPECT_EQ(60, _area->musicState().ambientDayVolume());
}

// Battle music on combat.

TEST_F(C04Test, party_damage_near_an_enemy_plays_battle_music_for_ten_seconds) {
    hostile(Faction::Friendly1, Faction::Hostile1);
    _area->musicState().setTracks("mus_area", "mus_bat");
    auto leader = addLeader();
    auto enemy = addCreature(Faction::Hostile1, glm::vec3(10.0f, 0.0f, 0.0f));

    leader->damage(1, enemy);
    EXPECT_TRUE(_area->musicState().isBattlePlaying());
    EXPECT_EQ("mus_bat", _game->musicResRef());

    _area->update(9.0f);
    EXPECT_TRUE(_area->musicState().isBattlePlaying());
    // Fresh damage extends the timer.
    enemy->damage(1, leader);
    _area->update(9.5f);
    EXPECT_TRUE(_area->musicState().isBattlePlaying());
    _area->update(0.6f);
    EXPECT_FALSE(_area->musicState().isBattlePlaying());
    EXPECT_EQ("mus_area", _game->musicResRef());
}

TEST_F(C04Test, no_battle_music_without_an_enemy_within_30m_or_outside_the_party) {
    hostile(Faction::Friendly1, Faction::Hostile1);
    _area->musicState().setTracks("mus_area", "mus_bat");
    auto leader = addLeader();
    auto farEnemy = addCreature(Faction::Hostile1, glm::vec3(31.0f, 0.0f, 0.0f));
    leader->damage(1, farEnemy);
    EXPECT_FALSE(_area->musicState().isBattlePlaying());

    auto stranger = addCreature(Faction::Friendly1, glm::vec3(30.0f, 0.0f, 0.0f));
    stranger->damage(1, farEnemy);
    EXPECT_FALSE(_area->musicState().isBattlePlaying());
}

// Sound objects.

TEST_F(C04Test, sound_object_setters_and_getters) {
    auto sound = newSound(0.1f, 0);
    auto arg = Variable::ofObject(sound->id());
    EXPECT_FLOAT_EQ(0.1f, call("SoundObjectGetPitchVariance", {arg}).floatValue);

    call("SoundObjectSetVolume", {arg, Variable::ofInt(200)});
    EXPECT_EQ(127, call("SoundObjectGetVolume", {arg}).intValue);
    call("SoundObjectSetVolume", {arg, Variable::ofInt(-5)});
    EXPECT_EQ(0, sound->volume());

    call("SoundObjectSetFixedVariance", {arg, Variable::ofFloat(1.5f)});
    EXPECT_FLOAT_EQ(1.5f, call("SoundObjectGetFixedVariance", {arg}).floatValue);
    EXPECT_FLOAT_EQ(1.5f, sound->basePitch());
    // Out of the audio layer's range: the recorded rate.
    call("SoundObjectSetFixedVariance", {arg, Variable::ofFloat(0.0f)});
    EXPECT_FLOAT_EQ(1.0f, sound->basePitch());

    call("SoundObjectSetPitchVariance", {arg, Variable::ofFloat(0.3f)});
    EXPECT_FLOAT_EQ(0.3f, sound->pitchVariation());
}

TEST_F(C04Test, sound_object_getters_answer_zero_for_other_objects) {
    auto creature = _game->newCreature();
    EXPECT_FLOAT_EQ(0.0f, call("SoundObjectGetPitchVariance", {Variable::ofObject(creature->id())}).floatValue);
    EXPECT_EQ(0, call("SoundObjectGetVolume", {Variable::ofObject(creature->id())}).intValue);
}

TEST_F(C04Test, sound_object_fade_and_stop_deactivates_at_once) {
    auto sound = newSound(0.0f, 0);
    ASSERT_TRUE(sound->isActive());
    call("SoundObjectFadeAndStop", {Variable::ofObject(sound->id()), Variable::ofFloat(1.5f)});
    EXPECT_FALSE(sound->isActive());
}

TEST_F(C04Test, sound_variance_draws_from_the_cosmetic_stream_only) {
    auto sound = newSound(0.2f, 10);
    uint64_t gameplayBefore = gameplayRandom().draws();
    uint64_t cosmeticBefore = cosmeticRandom().draws();
    sound->update(0.1f);
    EXPECT_EQ(gameplayBefore, gameplayRandom().draws());
    EXPECT_LT(cosmeticBefore, cosmeticRandom().draws());
}

// PlaySound.

TEST_F(C04Test, play_sound_plays_once_at_the_caller) {
    auto &clips = _engine.resourceModule().audioClips();
    auto clip = std::make_shared<AudioClip>();
    EXPECT_CALL(clips, get("cs_puzzledone")).WillRepeatedly(Return(clip));
    auto &mixer = _engine.audioModule().mixer();
    auto creature = addCreature(Faction::Friendly1, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_CALL(mixer, play(_, AudioType::Sound, 1.0f, false, Optional(glm::vec3(1.0f, 2.0f, 4.7f))))
        .Times(1)
        .WillOnce(Return(nullptr));
    call("PlaySound", {Variable::ofString("cs_puzzledone")}, creature);
    Mock::VerifyAndClearExpectations(&mixer);

    // Areas and the module have no place in the world: silent.
    EXPECT_CALL(mixer, play(_, _, _, _, _)).Times(0);
    call("PlaySound", {Variable::ofString("cs_puzzledone")}, _area);
}

// DuplicateHeadAppearance.

TEST_F(C04Test, duplicate_head_appearance_copies_the_head_row) {
    auto &twoDas = _engine.resourceModule().twoDas();
    ON_CALL(twoDas, get("appearance")).WillByDefault(Return(makeTable(
        {"label", "normalhead"},
        {{"a", "5"}, {"b", "12"}})));
    auto revan = _game->newCreature();
    auto pc = _game->newCreature();
    pc->setAppearance(1);
    EXPECT_EQ(0 + 5, revan->headRow());
    call("DuplicateHeadAppearance", {Variable::ofObject(revan->id()), Variable::ofObject(pc->id())});
    EXPECT_EQ(12, revan->headRow());
}

// PlayRoomAnimation, PlayVisualAreaEffect.

TEST_F(C04Test, play_room_animation_on_an_unknown_room_does_nothing) {
    EXPECT_FALSE(_area->playRoomAnimation("m12aa_01q", 1));
    EXPECT_NO_THROW(call("PlayRoomAnimation", {Variable::ofString("m12aa_01q"), Variable::ofInt(1)}));
}

TEST_F(C04Test, play_visual_area_effect_applies_at_the_location) {
    auto location = std::make_shared<Location>(glm::vec3(1.0f, 2.0f, 0.0f), 0.0f);
    EXPECT_NO_THROW(call("PlayVisualAreaEffect", {Variable::ofInt(1016), Variable::ofLocation(location)}));
}
