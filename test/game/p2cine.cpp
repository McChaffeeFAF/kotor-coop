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

// P2-Cine (playtest 1b items 1, 2, 11, 19): movie audio, dialog camera shots,
// the camera a node uses, and who a speaker faces. Behaviour notes:
// design note p2_cine.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/audio.h"
#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/movie.h"
#include "../fixtures/scene.h"

#include "reone/audio/clip.h"
#include "reone/audio/source.h"
#include "reone/game/game.h"
#include "reone/game/gui/dialog.h"
#include "reone/game/object/area.h"
#include "reone/game/object/camera/dialog.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/movie/movie.h"
#include "reone/resource/dialog.h"

using namespace reone;
using namespace reone::audio;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace reone::game {

class P2CineTestAccess {
public:
    static void begin(DialogGUI &gui, std::shared_ptr<Dialog> dialog, std::shared_ptr<Object> owner) {
        gui._dialog = std::move(dialog);
        gui._owner = std::move(owner);
        gui._currentSpeaker = gui.owner();
        gui._currentListener.reset();
        gui._previousSpeaker.reset();
        gui._previousListener.reset();
        gui._firstNodeShown = false;
    }

    // Shows `entry`: its speaker and listener, then the facing a dialog-camera
    // node applies.
    static void show(DialogGUI &gui, const Dialog::EntryReply &entry) {
        gui._currentEntry = &entry;
        gui.loadCurrentSpeaker();
        gui.applyFacing();
    }

    static std::shared_ptr<Object> speaker(DialogGUI &gui) { return gui._currentSpeaker.resolve(); }
    static std::shared_ptr<Object> listener(DialogGUI &gui) { return gui._currentListener.resolve(); }

    static int autoAngle(DialogGUI &gui) { return gui.nextAutoAngle(); }
    static void setCycle(DialogGUI &gui, int index, int step, bool up) {
        gui._angleCycle.index = index;
        gui._angleCycle.step = step;
        gui._angleCycle.up = up;
    }

    static void setEntry(DialogGUI &gui, std::shared_ptr<Dialog> dialog, const Dialog::EntryReply &entry) {
        gui._dialog = std::move(dialog);
        gui._currentEntry = &entry;
    }
};

} // namespace reone::game

namespace {

std::shared_ptr<AudioClip> makeClip() {
    auto clip = std::make_shared<AudioClip>();
    AudioClip::Frame frame;
    frame.sampleRate = 100;
    frame.samples.resize(100);
    clip->add(std::move(frame));
    return clip;
}

// --- Item 1: movie audio -----------------------------------------------------

TEST(P2CineMovie, finishing_a_movie_releases_its_audio) {
    TestEngine engine;
    engine.init();
    auto &mixer = static_cast<MockAudioMixer &>(engine.services().audio.mixer);
    std::weak_ptr<AudioSource> played;
    EXPECT_CALL(mixer, play(_, AudioType::Movie, _, _, _))
        .WillOnce(Invoke([&played](std::shared_ptr<AudioClip> clip, AudioType, float, bool, std::optional<glm::vec3>) {
            auto source = std::make_shared<AudioSource>(std::move(clip));
            played = source;
            return source;
        }));
    movie::Movie movie(engine.services().graphics, mixer);
    movie.setAudioClip(makeClip());
    movie.init();
    ASSERT_FALSE(played.expired());

    movie.finish();

    // The movie no longer holds (or plays) the source it started.
    EXPECT_TRUE(movie.isFinished());
    EXPECT_TRUE(played.expired());
}

TEST(P2CineMovie, destroying_a_movie_releases_its_audio) {
    TestEngine engine;
    engine.init();
    auto &mixer = static_cast<MockAudioMixer &>(engine.services().audio.mixer);
    std::weak_ptr<AudioSource> played;
    EXPECT_CALL(mixer, play(_, AudioType::Movie, _, _, _))
        .WillOnce(Invoke([&played](std::shared_ptr<AudioClip> clip, AudioType, float, bool, std::optional<glm::vec3>) {
            auto source = std::make_shared<AudioSource>(std::move(clip));
            played = source;
            return source;
        }));
    {
        movie::Movie movie(engine.services().graphics, mixer);
        movie.setAudioClip(makeClip());
        movie.init();
        ASSERT_FALSE(played.expired());
    }
    EXPECT_TRUE(played.expired());
}

TEST(P2CineMovie, a_click_during_a_movie_finishes_it) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto movie = std::make_shared<NiceMock<movie::MockMovie>>();
    EXPECT_CALL(engine.resourceModule().movies(), get("skip_me")).WillOnce(Return(movie));
    game.playVideo("skip_me");
    ASSERT_TRUE(game.movie());

    EXPECT_CALL(*movie, finish()).Times(1);
    game.handle(input::Event::newMouseButtonDown({input::MouseButton::Left, true, 1, 10, 10}));
}

// --- Item 2: dialog camera shots ----------------------------------------------

using Shot = DialogCamera::Shot;
using Angle = DialogCamera::Angle;

Shot shotOf(Angle angle, int side = 1) {
    Shot shot;
    shot.angle = angle;
    shot.side = side;
    return shot;
}

TEST(P2CineCamera, close_up_frames_the_speaker_from_half_a_metre_towards_the_listener) {
    glm::vec3 speaker(0.0f, 0.0f, 1.7f);
    glm::vec3 listener(2.0f, 0.0f, 1.7f);
    auto framing = DialogCamera::frame(shotOf(Angle::CloseUp), speaker, listener, glm::vec3(0.0f));

    // Target: 0.2 m in front of the speaker towards the listener, 4 cm lower.
    EXPECT_NEAR(framing.target.x, 0.2f, 1e-4f);
    EXPECT_NEAR(framing.target.y, 0.0f, 1e-4f);
    EXPECT_NEAR(framing.target.z, 1.66f, 1e-4f);
    // Camera: 0.5 m away, 30 degrees off the line, on the listener's side.
    EXPECT_NEAR(glm::distance(framing.eye, framing.target), 0.5f, 1e-4f);
    EXPECT_NEAR(framing.eye.z, framing.target.z, 1e-4f);
    EXPECT_GT(framing.eye.x, framing.target.x);
    glm::vec2 view(glm::normalize(glm::vec2(framing.target - framing.eye)));
    EXPECT_NEAR(glm::degrees(std::acos(glm::dot(view, glm::vec2(-1.0f, 0.0f)))), 30.0f, 0.01f);
}

TEST(P2CineCamera, close_up_range_moves_the_camera_out_and_up) {
    glm::vec3 speaker(0.0f, 0.0f, 1.7f);
    glm::vec3 listener(2.0f, 0.0f, 1.7f);
    Shot shot = shotOf(Angle::CloseUp);
    shot.closeUpRange = 1.0f;
    auto framing = DialogCamera::frame(shot, speaker, listener, glm::vec3(0.0f));
    EXPECT_NEAR(framing.target.z, 1.46f, 1e-4f);
    EXPECT_NEAR(framing.eye.z, 1.66f, 1e-4f);
    EXPECT_NEAR(glm::length(glm::vec2(framing.eye - framing.target)), 1.5f, 1e-4f);
}

TEST(P2CineCamera, wide_shot_stands_off_the_line_at_one_and_a_half_times_the_separation) {
    glm::vec3 speaker(0.0f);
    glm::vec3 listener(2.0f, 0.0f, 0.0f);
    auto side1 = DialogCamera::frame(shotOf(Angle::Wide, 1), speaker, listener, glm::vec3(0.0f));
    EXPECT_NEAR(side1.target.x, 1.0f, 1e-4f);
    EXPECT_NEAR(side1.target.z, -0.4f, 1e-4f);
    EXPECT_NEAR(side1.eye.x, 1.0f, 1e-4f);
    EXPECT_NEAR(std::abs(side1.eye.y), 3.0f, 1e-4f);
    EXPECT_NEAR(side1.eye.z, 0.2f, 1e-4f);
    // The other side mirrors it across the line.
    auto side2 = DialogCamera::frame(shotOf(Angle::Wide, 2), speaker, listener, glm::vec3(0.0f));
    EXPECT_NEAR(side2.eye.y, -side1.eye.y, 1e-4f);
}

TEST(P2CineCamera, over_the_shoulder_sits_behind_the_listener) {
    glm::vec3 speaker(0.0f);
    glm::vec3 listener(2.0f, 0.0f, 0.0f);
    auto framing = DialogCamera::frame(shotOf(Angle::OverShoulder), speaker, listener, glm::vec3(0.0f));
    // Target 30 % of the way from the listener to the speaker.
    EXPECT_NEAR(framing.target.x, 1.4f, 1e-4f);
    // The camera is beyond the listener, looking back at the speaker.
    EXPECT_GT(framing.eye.x, listener.x);
    EXPECT_LT(framing.eye.x, listener.x + 1.5f);
}

TEST(P2CineCamera, hold_keeps_the_camera_and_looks_at_the_speaker) {
    glm::vec3 speaker(1.0f, 1.0f, 1.5f);
    glm::vec3 hold(5.0f, 5.0f, 2.0f);
    Shot shot = shotOf(Angle::Hold);
    shot.targetHeightOffset = 0.25f;
    auto framing = DialogCamera::frame(shot, speaker, glm::vec3(3.0f, 1.0f, 1.5f), hold);
    EXPECT_EQ(framing.eye, hold);
    EXPECT_NEAR(framing.target.z, 1.75f, 1e-4f);
}

TEST(P2CineCamera, a_blocked_shot_falls_back_to_the_close_up) {
    glm::vec3 speaker(0.0f, 0.0f, 1.7f);
    glm::vec3 listener(2.0f, 0.0f, 1.7f);
    Shot wide = shotOf(Angle::Wide);
    wide.closeUpFallback = true;
    auto fallback = DialogCamera::frame(wide, speaker, listener, glm::vec3(0.0f));
    auto closeUp = DialogCamera::frame(shotOf(Angle::CloseUp), speaker, listener, glm::vec3(0.0f));
    EXPECT_EQ(fallback.eye, closeUp.eye);
    EXPECT_EQ(fallback.target, closeUp.target);
    // Within a metre of the speaker.
    EXPECT_LT(glm::distance(fallback.eye, speaker), 1.0f);
}

TEST(P2CineCamera, coincident_anchors_still_frame_a_close_up) {
    glm::vec3 point(3.0f, 3.0f, 1.0f);
    auto framing = DialogCamera::frame(shotOf(Angle::Wide), point, point, glm::vec3(0.0f));
    EXPECT_TRUE(std::isfinite(framing.eye.x) && std::isfinite(framing.eye.y) && std::isfinite(framing.eye.z));
    EXPECT_NEAR(glm::distance(framing.eye, framing.target), 0.5f, 1e-4f);
}

// --- Items 2 and 11: which camera a node uses -----------------------------------

class P2CineDialogTest : public Test {
protected:
    void SetUp() override {
        _engine.init();
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(_sceneGraph));
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);
        _gui = std::make_unique<DialogGUI>(*_game, _engine.services());
    }

    std::shared_ptr<Creature> addCreature(const std::string &tag, glm::vec3 position, float facing = 0.0f) {
        auto creature = _game->newCreature();
        creature->setTag(tag);
        creature->setPosition(position);
        creature->setFacing(facing);
        _area->add(creature);
        return creature;
    }

    std::shared_ptr<Creature> addPlayer(glm::vec3 position, float facing = 0.0f) {
        auto player = addCreature("player", position, facing);
        _game->party().addMember(kNpcPlayer, player);
        _game->party().setPlayer(player);
        return player;
    }

    TestEngine _engine;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::shared_ptr<Area> _area;
    std::unique_ptr<DialogGUI> _gui;
};

TEST_F(P2CineDialogTest, a_talk_node_in_a_camera_model_conversation_uses_the_dialog_camera) {
    auto dialog = std::make_shared<Dialog>();
    dialog->cameraModel = "m02af_c10_cam";
    dialog->entries.resize(3);
    dialog->entries[0].cameraAnimation = 1201; // the wake-up cutscene
    dialog->entries[1].cameraAngle = 1;        // Carth talking
    dialog->entries[2].cameraAngle = 6;
    dialog->entries[2].cameraId = 4;
    int cameraId = -1;

    P2CineTestAccess::setEntry(*_gui, dialog, dialog->entries[0]);
    EXPECT_EQ(_gui->getCamera(cameraId), CameraType::Animated);

    P2CineTestAccess::setEntry(*_gui, dialog, dialog->entries[1]);
    EXPECT_EQ(_gui->getCamera(cameraId), CameraType::Dialog);

    P2CineTestAccess::setEntry(*_gui, dialog, dialog->entries[2]);
    EXPECT_EQ(_gui->getCamera(cameraId), CameraType::Static);
    EXPECT_EQ(cameraId, 4);
}

TEST_F(P2CineDialogTest, a_camera_animation_needs_a_camera_model) {
    auto dialog = std::make_shared<Dialog>();
    dialog->entries.resize(1);
    dialog->entries[0].cameraAnimation = 1200;
    int cameraId = -1;
    P2CineTestAccess::setEntry(*_gui, dialog, dialog->entries[0]);
    EXPECT_EQ(_gui->getCamera(cameraId), CameraType::Dialog);
}

TEST_F(P2CineDialogTest, automatic_angles_walk_the_table_and_wrap) {
    // Walking up by 2 from row 17 runs past the end and restarts at row 0.
    P2CineTestAccess::setCycle(*_gui, 17, 2, true);
    EXPECT_EQ(P2CineTestAccess::autoAngle(*_gui), 1); // row 17
    EXPECT_EQ(P2CineTestAccess::autoAngle(*_gui), 1); // row 0
    EXPECT_EQ(P2CineTestAccess::autoAngle(*_gui), 1); // row 2
    EXPECT_EQ(P2CineTestAccess::autoAngle(*_gui), 2); // row 4
    // Walking down from row 1 by 3 wraps to the last row.
    P2CineTestAccess::setCycle(*_gui, 1, 3, false);
    EXPECT_EQ(P2CineTestAccess::autoAngle(*_gui), 3);
    EXPECT_EQ(P2CineTestAccess::autoAngle(*_gui), 1); // row 18
}

// --- Item 19: speaker and listener, and facing ------------------------------------

TEST_F(P2CineDialogTest, an_empty_listener_is_the_player_then_whoever_spoke_last) {
    auto player = addPlayer(glm::vec3(0.0f, -3.0f, 0.0f));
    auto trask = addCreature("end_trask", glm::vec3(0.0f));
    auto carth = addCreature("carth", glm::vec3(2.0f, 0.0f, 0.0f));
    auto dialog = std::make_shared<Dialog>();
    dialog->entries.resize(4);
    dialog->entries[0].speaker = "end_trask";
    dialog->entries[1].speaker = "carth";
    dialog->entries[2].speaker = "carth";
    dialog->entries[3].speaker = "end_trask";
    dialog->entries[3].listener = "player";
    P2CineTestAccess::begin(*_gui, dialog, trask);

    P2CineTestAccess::show(*_gui, dialog->entries[0]);
    EXPECT_EQ(P2CineTestAccess::listener(*_gui), player);
    P2CineTestAccess::show(*_gui, dialog->entries[1]);
    EXPECT_EQ(P2CineTestAccess::speaker(*_gui), carth);
    EXPECT_EQ(P2CineTestAccess::listener(*_gui), trask);
    P2CineTestAccess::show(*_gui, dialog->entries[2]);
    EXPECT_EQ(P2CineTestAccess::listener(*_gui), player);
    P2CineTestAccess::show(*_gui, dialog->entries[3]);
    EXPECT_EQ(P2CineTestAccess::listener(*_gui), player);
}

TEST_F(P2CineDialogTest, npcs_talking_to_each_other_do_not_turn_the_player) {
    // The player faces +y; Trask at the origin faces +y; Carth two metres
    // east of Trask faces west, towards him.
    auto player = addPlayer(glm::vec3(1.0f, -3.0f, 0.0f), 0.0f);
    auto trask = addCreature("end_trask", glm::vec3(0.0f), 0.0f);
    auto carth = addCreature("carth", glm::vec3(2.0f, 0.0f, 0.0f), glm::radians(90.0f));
    auto dialog = std::make_shared<Dialog>();
    dialog->entries.resize(2);
    dialog->entries[0].speaker = "end_trask";
    dialog->entries[0].listener = "carth";
    dialog->entries[1].speaker = "carth";
    P2CineTestAccess::begin(*_gui, dialog, trask);
    float playerFacing = player->getFacing();

    P2CineTestAccess::show(*_gui, dialog->entries[0]);
    // Trask, speaking to Carth, turns to him (fully: no head look-at yet).
    EXPECT_NEAR(glm::degrees(trask->getFacing()), -90.0f, 0.01f);
    // Carth already faces Trask.
    EXPECT_NEAR(glm::degrees(carth->getFacing()), 90.0f, 0.01f);
    EXPECT_FLOAT_EQ(player->getFacing(), playerFacing);

    P2CineTestAccess::show(*_gui, dialog->entries[1]);
    EXPECT_FLOAT_EQ(player->getFacing(), playerFacing);
}

TEST_F(P2CineDialogTest, orientation_and_head_follow_locks) {
    addPlayer(glm::vec3(0.0f, -3.0f, 0.0f));
    auto trask = addCreature("end_trask", glm::vec3(0.0f), 0.0f);
    auto carth = addCreature("carth", glm::vec3(2.0f, 0.0f, 0.0f), 0.0f);
    auto dialog = std::make_shared<Dialog>();
    dialog->entries.resize(1);
    dialog->entries[0].speaker = "end_trask";
    dialog->entries[0].listener = "carth";
    // Trask keeps his facing; Carth, without head follow, turns fully.
    DialogLocks::set(_game->dialogLocks().orientation, trask->id(), true);
    DialogLocks::set(_game->dialogLocks().headFollow, carth->id(), true);
    P2CineTestAccess::begin(*_gui, dialog, trask);

    P2CineTestAccess::show(*_gui, dialog->entries[0]);

    EXPECT_FLOAT_EQ(trask->getFacing(), 0.0f);
    EXPECT_NEAR(glm::degrees(carth->getFacing()), 90.0f, 0.01f);
}

} // namespace
