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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"

#include "reone/audio/clip.h"
#include "reone/audio/mixer.h"
#include "reone/audio/source.h"
#include "reone/game/action/pauseconversation.h"
#include "reone/game/action/resumeconversation.h"
#include "reone/game/console.h"
#include "reone/input/event.h"
#include "reone/game/game.h"
#include "reone/game/gui/conversation.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/resource/dialog.h"

using namespace reone;
using namespace reone::audio;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

class StubConsole : public IConsole, boost::noncopyable {
public:
    void registerCommand(std::string name, std::string description, CommandHandler handler) override {}
    void printLine(const std::string &text) override {}
};

class TestConversation : public Conversation {
public:
    TestConversation(Game &game, ServicesView &services) :
        Conversation(game, services) {
    }

    const std::string &currentText() const {
        return _currentEntry->text;
    }

    int entryLoadCount() const {
        return _entryLoadCount;
    }

    int entryEndCount() const {
        return _entryEndCount;
    }

    bool isPaused() const {
        return _paused;
    }

    void pickReplyForTest(int index) {
        pickReply(index);
    }

    void setGUIForTest(std::shared_ptr<gui::IGUI> gui) {
        _gui = std::move(gui);
    }

    void pauseOnFirstEntryLoad() {
        _pauseOnFirstEntryLoad = true;
    }

    const std::string &barkText() const {
        return _barkText;
    }

    int barkCount() const {
        return _barkCount;
    }

    int finishCount() const {
        return _finishCount;
    }

protected:
    void setReplyLines(std::vector<std::string> lines) override {}
    void setMessage(std::string message) override {}

    void setBarkText(std::string text, float duration) override {
        _barkText = std::move(text);
        ++_barkCount;
    }

    void onLoadEntry() override {
        ++_entryLoadCount;
        if (_pauseOnFirstEntryLoad && _entryLoadCount == 1) {
            pause();
        }
    }

    void onEntryEnded() override {
        ++_entryEndCount;
    }

    void onFinish() override {
        ++_finishCount;
    }

private:
    int _entryLoadCount {0};
    int _entryEndCount {0};
    bool _pauseOnFirstEntryLoad {false};
    std::string _barkText;
    int _barkCount {0};
    int _finishCount {0};
};

std::shared_ptr<Dialog> makeDialog(int firstEntryDelay = 1, bool voiced = false) {
    auto dialog = std::make_shared<Dialog>();
    dialog->resRef = "pause_test";
    Dialog::EntryReplyLink startLink;
    startLink.index = 0;
    dialog->startEntries.push_back(startLink);
    dialog->entries.resize(2);
    dialog->replies.resize(2);

    auto &firstEntry = dialog->entries[0];
    firstEntry.text = "first";
    firstEntry.delay = firstEntryDelay;
    Dialog::EntryReplyLink automaticReplyLink;
    automaticReplyLink.index = 0;
    firstEntry.replies.push_back(automaticReplyLink);
    if (voiced) {
        firstEntry.sound = "test_voice";
    }

    auto &automaticReply = dialog->replies[0];
    Dialog::EntryReplyLink secondEntryLink;
    secondEntryLink.index = 1;
    automaticReply.entries.push_back(secondEntryLink);

    auto &secondEntry = dialog->entries[1];
    secondEntry.text = "second";
    secondEntry.delay = 100;
    Dialog::EntryReplyLink visibleReplyLink;
    visibleReplyLink.index = 1;
    secondEntry.replies.push_back(visibleReplyLink);

    dialog->replies[1].text = "Continue";
    return dialog;
}

// A one-liner: a single start entry whose only eligible reply is empty and
// terminal. Both nodes carry an authored action.
std::shared_ptr<Dialog> makeOneLinerDialog(std::string entryScript, std::string replyScript) {
    auto dialog = std::make_shared<Dialog>();
    dialog->resRef = "one_liner_test";
    Dialog::EntryReplyLink startLink;
    startLink.index = 0;
    dialog->startEntries.push_back(startLink);
    dialog->entries.resize(1);
    dialog->replies.resize(1);

    auto &entry = dialog->entries[0];
    entry.text = "bark";
    entry.delay = 1;
    entry.script = std::move(entryScript);
    Dialog::EntryReplyLink terminalReplyLink;
    terminalReplyLink.index = 0;
    entry.replies.push_back(terminalReplyLink);

    // Empty text and no child entries -- this is what makes it a one-liner.
    dialog->replies[0].script = std::move(replyScript);
    return dialog;
}

// The one-liner shape under test: the start entry has \p replyLinks links to
// the same terminal reply (0 gives an entry with no replies at all).
std::shared_ptr<Dialog> makeShapedDialog(ConversationType type, std::string replyText, size_t replyLinks = 1) {
    auto dialog = makeOneLinerDialog("", "");
    dialog->resRef = "shaped_test";
    dialog->conversationType = type;
    dialog->replies[0].text = std::move(replyText);
    if (replyLinks == 0) {
        dialog->entries[0].replies.clear();
    }
    return dialog;
}

std::shared_ptr<AudioClip> makeOneSecondClip() {
    auto clip = std::make_shared<AudioClip>();
    AudioClip::Frame frame;
    frame.sampleRate = 100;
    frame.samples.resize(100);
    clip->add(std::move(frame));
    return clip;
}

class ConversationTest : public Test {
protected:
    void SetUp() override {
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        // Gives the game a real script runner over MockScripts, so dialogue
        // actions can be observed as requests for their script.
        _game->initLocalServices();
        _conversation = std::make_unique<TestConversation>(*_game, _engine.services());
    }

    void startSilent(int delay = 1) {
        _conversation->start(makeDialog(delay), nullptr);
    }

    void startVoiced(int delay = -1, bool skippable = false) {
        auto clip = makeOneSecondClip();
        auto source = std::make_shared<AudioSource>(clip);

        EXPECT_CALL(static_cast<resource::MockLips &>(_engine.services().resource.lips), get("test_voice"))
            .WillOnce(Return(nullptr));
        EXPECT_CALL(_engine.resourceModule().audioClips(), get("test_voice"))
            .WillOnce(Return(clip));
        EXPECT_CALL(static_cast<audio::MockAudioMixer &>(_engine.services().audio.mixer), play(_, AudioType::Voice, _, _, _))
            .WillOnce(Return(source));

        auto dialog = makeDialog(delay, true);
        dialog->skippable = skippable;
        _conversation->start(dialog, nullptr);
    }

    void attachConversation(Game::Screen screen) {
        TestGameModule::setConversation(*_game, _conversation.get());
        TestGameModule::setCurrentScreen(*_game, static_cast<int>(screen));
    }

    TestEngine _engine;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::unique_ptr<TestConversation> _conversation;
};

TEST_F(ConversationTest, ordinary_static_dialogue_keeps_its_own_gui) {
    auto dialog = makeDialog();
    dialog->entries[0].cameraId = 1;
    auto gui = std::make_shared<NiceMock<gui::MockGUI>>();
    _conversation->setGUIForTest(gui);
    EXPECT_CALL(_engine.guiModule().guis(), get(_, _)).Times(0);
    _conversation->start(dialog, nullptr);
    int cameraId = 0;
    EXPECT_EQ(_conversation->getCamera(cameraId), CameraType::Static);
    EXPECT_EQ(cameraId, 1);
    EXPECT_CALL(*gui, render()).Times(1);
    _conversation->render();
}

TEST_F(ConversationTest, animated_dialogue_keeps_its_own_gui) {
    auto dialog = makeDialog();
    dialog->cameraModel = "camera_model";
    // Only an entry naming a camera animation plays the camera model (design note p2_cine).
    dialog->entries[0].cameraAnimation = 1200;
    auto gui = std::make_shared<NiceMock<gui::MockGUI>>();
    _conversation->setGUIForTest(gui);
    EXPECT_CALL(_engine.guiModule().guis(), get(_, _)).Times(0);
    _conversation->start(dialog, nullptr);
    int cameraId = 0;
    EXPECT_EQ(_conversation->getCamera(cameraId), CameraType::Animated);
    EXPECT_CALL(*gui, render()).Times(1);
    _conversation->render();
}

TEST_F(ConversationTest, game_pause_is_harmless_without_a_conversation) {
    EXPECT_FALSE(_game->isConversationActive());

    _game->pauseConversation();
}

TEST_F(ConversationTest, game_resume_is_harmless_without_a_conversation) {
    EXPECT_FALSE(_game->isConversationActive());

    _game->resumeConversation();
}

TEST_F(ConversationTest, pause_and_resume_actions_complete_without_a_conversation) {
    auto actor = _game->newCreature();
    auto pause = _game->newAction<PauseConversationAction>();
    auto resume = _game->newAction<ResumeConversationAction>();

    pause->execute(pause, *actor, 0.0f);
    resume->execute(resume, *actor, 0.0f);

    EXPECT_TRUE(pause->isCompleted());
    EXPECT_TRUE(resume->isCompleted());
}

TEST_F(ConversationTest, queued_pause_and_resume_actions_are_consumed_without_a_conversation) {
    auto actor = _game->newCreature();
    actor->addAction(_game->newAction<PauseConversationAction>());
    actor->addAction(_game->newAction<ResumeConversationAction>());

    actor->update(0.0f);
    actor->update(0.0f);
    actor->update(0.0f);

    EXPECT_TRUE(actor->actions().empty());
}

TEST_F(ConversationTest, game_pause_and_resume_ignore_a_retained_inactive_conversation) {
    startSilent();
    attachConversation(Game::Screen::InGame);

    ASSERT_FALSE(_conversation->isPaused());
    _game->pauseConversation();
    EXPECT_FALSE(_conversation->isPaused());

    _conversation->pause();
    ASSERT_TRUE(_conversation->isPaused());
    _game->resumeConversation();
    EXPECT_TRUE(_conversation->isPaused());
}

TEST_F(ConversationTest, game_pause_and_resume_control_an_active_conversation) {
    startSilent();
    attachConversation(Game::Screen::Conversation);

    ASSERT_FALSE(_conversation->isPaused());
    _game->pauseConversation();
    EXPECT_TRUE(_conversation->isPaused());

    _game->resumeConversation();
    EXPECT_FALSE(_conversation->isPaused());
}

TEST_F(ConversationTest, silent_entry_timer_expiry_does_not_advance_while_paused) {
    startSilent();
    _conversation->pause();

    _conversation->updateSimulation(2.0f);

    EXPECT_EQ("first", _conversation->currentText());
    EXPECT_EQ(1, _conversation->entryLoadCount());
    EXPECT_EQ(0, _conversation->entryEndCount());
}

TEST_F(ConversationTest, voice_completion_does_not_advance_while_paused) {
    startVoiced();
    _conversation->pause();

    _conversation->updateSimulation(0.1f);

    EXPECT_EQ("first", _conversation->currentText());
    EXPECT_EQ(1, _conversation->entryLoadCount());
    EXPECT_EQ(0, _conversation->entryEndCount());
}

TEST_F(ConversationTest, resume_after_expiry_advances_on_next_update_exactly_once) {
    startSilent();
    _conversation->pause();
    _conversation->updateSimulation(2.0f);

    _conversation->resume();
    _conversation->updateSimulation(0.0f);
    _conversation->updateSimulation(0.0f);

    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(2, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, resume_before_expiry_preserves_elapsed_timing) {
    startSilent();
    _conversation->pause();
    _conversation->updateSimulation(0.4f);

    _conversation->resume();
    _conversation->updateSimulation(0.5f);
    EXPECT_EQ("first", _conversation->currentText());

    _conversation->updateSimulation(0.2f);
    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, unpaused_silent_entry_retains_automatic_progression) {
    startSilent();

    _conversation->updateSimulation(1.0f);

    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(2, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, unpaused_voiced_entry_retains_completion_progression) {
    startVoiced();

    // A voiced entry ends when its clip has played for its full length in
    // simulation time; the mixer's wall-clock isPlaying flag is not consulted.
    _conversation->updateSimulation(0.5f);
    EXPECT_EQ("first", _conversation->currentText());
    _conversation->updateSimulation(0.5f);

    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(2, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, repeated_updates_while_paused_do_not_advance_or_duplicate_events) {
    startSilent();
    _conversation->pause();

    for (int i = 0; i < 10; ++i) {
        _conversation->updateSimulation(1.0f);
    }

    EXPECT_EQ("first", _conversation->currentText());
    EXPECT_EQ(1, _conversation->entryLoadCount());
    EXPECT_EQ(0, _conversation->entryEndCount());
}

TEST_F(ConversationTest, repeated_pause_and_resume_calls_are_safe_and_deterministic) {
    startSilent();
    _conversation->pause();
    _conversation->pause();
    _conversation->updateSimulation(0.4f);

    _conversation->resume();
    _conversation->resume();
    _conversation->updateSimulation(0.5f);
    EXPECT_EQ("first", _conversation->currentText());

    _conversation->updateSimulation(0.2f);
    _conversation->pause();
    _conversation->pause();
    _conversation->resume();
    _conversation->resume();
    _conversation->updateSimulation(0.0f);

    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(2, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, automatic_skip_does_not_bypass_a_paused_entry) {
    Conversation::AutoSkip autoSkip;
    autoSkip.enabled = true;
    autoSkip.entries.push(true);
    _conversation->setAutoSkip(&autoSkip);
    _conversation->pauseOnFirstEntryLoad();

    _conversation->start(makeDialog(), nullptr);

    EXPECT_EQ("first", _conversation->currentText());
    EXPECT_EQ(1, _conversation->entryLoadCount());
    EXPECT_EQ(0, _conversation->entryEndCount());
    EXPECT_TRUE(autoSkip.entries.empty());

    _conversation->resume();
    _conversation->updateSimulation(1.0f);

    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(2, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, unpaused_automatic_skip_retains_immediate_progression) {
    Conversation::AutoSkip autoSkip;
    autoSkip.enabled = true;
    autoSkip.entries.push(true);
    _conversation->setAutoSkip(&autoSkip);

    _conversation->start(makeDialog(), nullptr);

    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(2, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->entryEndCount());
    EXPECT_TRUE(autoSkip.entries.empty());
}

TEST_F(ConversationTest, entry_pause_then_external_resume_advances_next_entry_once) {
    _conversation->pauseOnFirstEntryLoad();
    _conversation->start(makeDialog(), nullptr);

    _conversation->updateSimulation(2.0f);
    EXPECT_EQ("first", _conversation->currentText());
    EXPECT_EQ(0, _conversation->entryEndCount());

    _conversation->resume();
    _conversation->updateSimulation(0.0f);
    _conversation->updateSimulation(0.0f);

    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(2, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, replacing_a_paused_conversation_starts_the_new_session_unpaused) {
    startSilent();
    _conversation->pause();

    _conversation->start(makeDialog(), nullptr);
    EXPECT_FALSE(_conversation->isPaused());

    _conversation->updateSimulation(1.0f);
    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(3, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, finishing_a_paused_conversation_does_not_leak_pause_to_the_next_session) {
    startSilent();
    _conversation->updateSimulation(1.0f);
    _conversation->pause();

    _conversation->pickReplyForTest(0);
    EXPECT_FALSE(_conversation->isPaused());

    startSilent();
    _conversation->updateSimulation(1.0f);
    _conversation->updateSimulation(0.0f);

    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(4, _conversation->entryLoadCount());
    EXPECT_EQ(2, _conversation->entryEndCount());
}

TEST_F(ConversationTest, finishing_leaves_a_screen_opened_by_a_reply_script_alone) {
    // A reply script can hand the screen to something else before the
    // conversation ends -- PlayPazaak opens the pazaak board from a dialogue
    // action -- so finishing must not pull the screen back to the world.
    startSilent();
    _conversation->updateSimulation(1.0f);

    _conversation->pickReplyForTest(0);

    EXPECT_EQ(Game::Screen::None, _game->currentScreen());
}

TEST_F(ConversationTest, module_transition_cleanup_clears_pause_before_the_next_session) {
    startSilent();
    _conversation->pause();

    _conversation->cleanupForModuleTransition();
    EXPECT_FALSE(_conversation->isPaused());

    startSilent();
    _conversation->updateSimulation(1.0f);

    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(3, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, one_liner_runs_entry_then_terminal_reply_actions_exactly_once) {
    auto &scripts = _engine.resourceModule().scripts();
    InSequence seq;
    EXPECT_CALL(scripts, get("one_entry")).WillOnce(Return(nullptr));
    EXPECT_CALL(scripts, get("one_reply")).WillOnce(Return(nullptr));

    _conversation->start(makeOneLinerDialog("one_entry", "one_reply"), nullptr);
}

TEST_F(ConversationTest, one_liner_with_no_entry_action_still_runs_the_terminal_reply_action) {
    // The authored action commonly sits only on the terminal reply, so running
    // the entry action alone would still drop it.
    auto &scripts = _engine.resourceModule().scripts();
    EXPECT_CALL(scripts, get("one_reply")).WillOnce(Return(nullptr));

    _conversation->start(makeOneLinerDialog("", "one_reply"), nullptr);
}

TEST_F(ConversationTest, one_liner_presents_its_entry_and_completes_without_opening_the_gui) {
    auto &scripts = _engine.resourceModule().scripts();
    EXPECT_CALL(scripts, get(_)).WillRepeatedly(Return(nullptr));

    _conversation->start(makeOneLinerDialog("one_entry", "one_reply"), nullptr);

    // Barked, not presented through the conversation GUI, and already over.
    EXPECT_EQ("bark", _conversation->barkText());
    EXPECT_EQ(1, _conversation->barkCount());
    EXPECT_EQ("bark", _conversation->currentText());
    EXPECT_EQ(Game::Screen::None, _game->currentScreen());
    EXPECT_EQ(1, _conversation->entryLoadCount());
}

TEST_F(ConversationTest, completed_one_liner_does_not_run_its_actions_a_second_time) {
    auto &scripts = _engine.resourceModule().scripts();
    EXPECT_CALL(scripts, get("one_entry")).WillOnce(Return(nullptr));
    EXPECT_CALL(scripts, get("one_reply")).WillOnce(Return(nullptr));

    _conversation->start(makeOneLinerDialog("one_entry", "one_reply"), nullptr);

    // The one-liner is already resolved; further ticks must not re-pick it.
    for (int i = 0; i < 5; ++i) {
        _conversation->updateSimulation(1.0f);
    }
    EXPECT_EQ(1, _conversation->barkCount());
}

TEST_F(ConversationTest, one_liner_entry_action_starting_a_conversation_keeps_the_new_session) {
    auto &scripts = _engine.resourceModule().scripts();
    auto replacement = makeDialog();

    // Stand in for an action script that starts another conversation: the
    // replacement happens at exactly the point the real script would run.
    EXPECT_CALL(scripts, get("one_entry")).WillOnce(Invoke([&](const std::string &) {
        _conversation->start(replacement, nullptr);
        return nullptr;
    }));
    // The old reply belongs to a conversation that no longer exists.
    EXPECT_CALL(scripts, get("one_reply")).Times(0);

    _conversation->start(makeOneLinerDialog("one_entry", "one_reply"), nullptr);

    // The new conversation is live and presented once by itself: the old one
    // must not have gone on to bark, reload or otherwise drive it.
    EXPECT_EQ("first", _conversation->currentText());
    EXPECT_EQ(1, _conversation->entryLoadCount());
    EXPECT_EQ(0, _conversation->barkCount());
    _conversation->updateSimulation(1.0f);
    EXPECT_EQ("second", _conversation->currentText());
}

TEST_F(ConversationTest, one_liner_reply_action_starting_a_conversation_keeps_the_new_session) {
    auto &scripts = _engine.resourceModule().scripts();
    auto replacement = makeDialog();

    EXPECT_CALL(scripts, get("one_reply")).WillOnce(Invoke([&](const std::string &) {
        _conversation->start(replacement, nullptr);
        return nullptr;
    }));

    _conversation->start(makeOneLinerDialog("", "one_reply"), nullptr);

    // Only the replacement itself ended the one-liner. Had the old conversation
    // carried on to finish, it would have ended the new session instead.
    EXPECT_EQ(1, _conversation->finishCount());
    EXPECT_EQ("first", _conversation->currentText());
    _conversation->updateSimulation(1.0f);
    EXPECT_EQ("second", _conversation->currentText());
}

TEST_F(ConversationTest, fade_hold_is_consumed_before_entry_action_and_not_after_it) {
    auto &fade = _game->globalFade();
    auto arrival = fade.beginArrival();
    auto dialog = makeDialog();
    dialog->entries[0].script = "authored_out";
    EXPECT_CALL(_engine.resourceModule().scripts(), get("authored_out"))
        .WillOnce(Invoke([&](const std::string &) {
            EXPECT_FALSE(fade.heldForDialog());
            EXPECT_LT(fade.opacity(), 1.0f); // in(0,1), including initial tenth
            fade.request(GlobalFade::Direction::Out);
            return nullptr;
        }));
    _conversation->start(dialog, nullptr);
    fade.settleArrival(arrival);
    fade.update(2);
    EXPECT_FLOAT_EQ(1, fade.opacity());
    EXPECT_EQ(1, _conversation->entryLoadCount());
}

TEST_F(ConversationTest, valid_empty_start_consumes_current_hold_without_a_timer) {
    auto &fade = _game->globalFade();
    fade.request(GlobalFade::Direction::Out);
    fade.holdForDialog();
    auto dialog = std::make_shared<Dialog>();
    dialog->resRef = "empty_start";
    _conversation->start(dialog, nullptr);
    EXPECT_FALSE(fade.heldForDialog());
    EXPECT_FALSE(fade.dialogPending());
    fade.update(1);
    EXPECT_FLOAT_EQ(0, fade.opacity());
    EXPECT_EQ(1, _conversation->finishCount());
}

TEST_F(ConversationTest, same_resource_entry_replacement_does_not_execute_old_second_script) {
    auto &fade = _game->globalFade();
    auto dialog = makeDialog();
    dialog->entries[0].script = "replace_same";
    dialog->entries[0].script2 = "second_action";
    bool replaced = false;
    EXPECT_CALL(_engine.resourceModule().scripts(), get("replace_same"))
        .Times(2).WillRepeatedly(Invoke([&](const std::string &) {
            if (!replaced) {
                replaced = true;
                fade.holdForDialog();
                _conversation->start(dialog, nullptr);
                // A new hold belongs to the replacement, not the old stack.
                fade.holdForDialog();
                fade.request(GlobalFade::Direction::Out);
            }
            return nullptr;
        }));
    EXPECT_CALL(_engine.resourceModule().scripts(), get("second_action")).Times(1).WillOnce(Return(nullptr));
    _conversation->start(dialog, nullptr);
    EXPECT_EQ(1, _conversation->entryLoadCount());
    EXPECT_TRUE(fade.heldForDialog());
    EXPECT_FLOAT_EQ(1, fade.opacity());
}

TEST_F(ConversationTest, same_resource_start_condition_replacement_cannot_finish_new_start) {
    auto &fade = _game->globalFade();
    auto dialog = makeDialog();
    dialog->startEntries[0].active = "replace_condition";
    EXPECT_CALL(_engine.resourceModule().scripts(), get("replace_condition"))
        .WillOnce(Invoke([&](const std::string &) {
            // Same object identity, different invocation. The old false
            // condition must not terminate the replacement's valid entry.
            dialog->startEntries[0].active.clear();
            _conversation->start(dialog, nullptr);
            fade.holdForDialog();
            fade.request(GlobalFade::Direction::Out);
            return nullptr;
        }));
    fade.holdForDialog();
    _conversation->start(dialog, nullptr);
    EXPECT_EQ(1, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->finishCount()); // replacing old startup only
    EXPECT_TRUE(fade.heldForDialog());
    EXPECT_TRUE(fade.dialogPending());
    EXPECT_FLOAT_EQ(1, fade.opacity());
}

TEST_F(ConversationTest, reply_condition_replacement_cannot_consume_replacement_hold) {
    auto dialog = makeDialog();
    dialog->entries[0].replies[0].active = "replace_reply_condition";
    auto &fade = _game->globalFade();
    EXPECT_CALL(_engine.resourceModule().scripts(), get("replace_reply_condition"))
        .WillOnce(Invoke([&](const std::string &) {
            dialog->entries[0].replies[0].active.clear();
            _conversation->start(dialog, nullptr);
            fade.holdForDialog();
            return nullptr;
        }));
    _conversation->start(dialog, nullptr);
    EXPECT_EQ(1, _conversation->entryLoadCount());
    EXPECT_TRUE(fade.heldForDialog());
}

TEST_F(ConversationTest, ordinary_close_and_bark_do_not_release_intentional_black_without_hold) {
    auto &fade = _game->globalFade();
    fade.request(GlobalFade::Direction::Out);
    _conversation->start(makeOneLinerDialog("", ""), nullptr);
    EXPECT_EQ(1, _conversation->barkCount());
    EXPECT_FLOAT_EQ(1, fade.opacity());
    startSilent();
    _conversation->cleanupForModuleTransition();
    EXPECT_FLOAT_EQ(1, fade.opacity());
    EXPECT_FALSE(fade.heldForDialog());
}

TEST_F(ConversationTest, bark_consumes_hold_even_when_script_lock_rejects_reveal) {
    auto &fade = _game->globalFade();
    fade.request(GlobalFade::Direction::Out);
    fade.holdForDialog();
    fade.lockUntilScript();
    _conversation->start(makeOneLinerDialog("", ""), nullptr);
    EXPECT_EQ(1, _conversation->barkCount());
    EXPECT_FALSE(fade.heldForDialog());
    EXPECT_TRUE(fade.locked());
    EXPECT_FLOAT_EQ(1, fade.opacity());
}

// --- the one-liner shape (upstream #356) ---------------------------------

TEST_F(ConversationTest, one_liner_reply_text_does_not_matter) {
    _conversation->start(makeShapedDialog(ConversationType::Cinematic, "Goodbye."), nullptr);
    EXPECT_EQ(1, _conversation->barkCount());
}

TEST_F(ConversationTest, one_liner_reply_whose_condition_fails_still_runs_its_action) {
    auto &scripts = _engine.resourceModule().scripts();
    EXPECT_CALL(scripts, get("one_reply")).WillOnce(Return(nullptr));
    EXPECT_CALL(scripts, get("never")).WillRepeatedly(Return(nullptr));
    auto dialog = makeOneLinerDialog("", "one_reply");
    dialog->entries[0].replies[0].active = "never"; // the link's condition script
    _conversation->start(dialog, nullptr);
    EXPECT_EQ(1, _conversation->barkCount());
}

TEST_F(ConversationTest, computer_conversation_is_a_one_liner_in_k1) {
    _conversation->start(makeShapedDialog(ConversationType::Computer, ""), nullptr);
    EXPECT_EQ(1, _conversation->barkCount());
}

TEST_F(ConversationTest, other_conversation_types_are_never_one_liners) {
    _conversation->start(makeShapedDialog(static_cast<ConversationType>(2), ""), nullptr);
    EXPECT_EQ(0, _conversation->barkCount());
}

TEST_F(ConversationTest, entry_with_no_replies_is_not_a_one_liner_yet) {
    _conversation->start(makeShapedDialog(ConversationType::Cinematic, "", 0), nullptr);
    EXPECT_EQ(0, _conversation->barkCount());
}

TEST_F(ConversationTest, entry_with_a_reply_leading_on_is_not_a_one_liner) {
    auto dialog = makeShapedDialog(ConversationType::Cinematic, "");
    dialog->entries.resize(2);
    dialog->entries[1].text = "next";
    Dialog::EntryReplyLink onward;
    onward.index = 1;
    dialog->replies[0].entries.push_back(onward);
    _conversation->start(dialog, nullptr);
    EXPECT_EQ(0, _conversation->barkCount());
}

TEST_F(ConversationTest, one_liner_still_awards_its_journal_entries) {
    auto dialog = makeOneLinerDialog("", "");
    dialog->entries[0].quest = "entry_quest";
    dialog->entries[0].questEntry = 10;
    dialog->replies[0].quest = "reply_quest";
    dialog->replies[0].questEntry = 20;
    _conversation->start(dialog, nullptr);
    EXPECT_EQ(1, _conversation->barkCount());
    EXPECT_EQ(10, _game->journal().getEntryState("entry_quest"));
    EXPECT_EQ(20, _game->journal().getEntryState("reply_quest"));
}

TEST_F(ConversationTest, sole_reply_that_resolves_to_nothing_is_taken_when_the_entry_ends) {
    auto dialog = makeDialog();
    dialog->entries[0].replies.resize(1);
    dialog->entries[0].replies[0].index = 0;
    dialog->replies[0].text = "";
    dialog->replies[0].entries.clear();
    auto &scripts = _engine.resourceModule().scripts();
    dialog->replies[0].script = "auto_reply";
    EXPECT_CALL(scripts, get("auto_reply")).WillOnce(Return(nullptr));
    dialog->conversationType = static_cast<ConversationType>(2); // not a one-liner: opens normally
    _conversation->start(dialog, nullptr);
    _conversation->updateSimulation(10.0f);
}

TEST(ConversationTslShape, computer_conversation_is_not_a_one_liner_in_tsl) {
    TestEngine engine;
    StubConsole console;
    engine.init();
    Game game(GameID::TSL, std::filesystem::path {}, engine.options(), engine.services(), console);
    game.initLocalServices();
    TestConversation conversation(game, engine.services());
    conversation.start(makeShapedDialog(ConversationType::Computer, ""), nullptr);
    EXPECT_EQ(0, conversation.barkCount());
    TestConversation cinematic(game, engine.services());
    cinematic.start(makeShapedDialog(ConversationType::Cinematic, ""), nullptr);
    EXPECT_EQ(1, cinematic.barkCount());
}


} // namespace

void reone::game::TestGameModule::setConversationStepPending(Game &game, bool pending) {
    game._conversationStepPending = pending;
}

namespace {

constexpr float kTickSeconds = 1.0f / 30.0f;

} // namespace

// Cutscene ordering (design note cutscene_timing). end_trask01 entry 54 has
// Delay 0 and a script that queues ActionPauseConversation on Trask; the
// conversation starts from Trask's action, inside the tick's world step.

TEST_F(ConversationTest, an_entry_started_in_the_world_step_does_not_end_in_that_tick) {
    TestGameModule::setConversationStepPending(*_game, true);
    startSilent(0);
    TestGameModule::setConversationStepPending(*_game, false);

    _conversation->updateSimulation(kTickSeconds); // the same tick's conversation step
    EXPECT_EQ("first", _conversation->currentText());
    EXPECT_EQ(0, _conversation->entryEndCount());

    _conversation->updateSimulation(kTickSeconds); // the next tick
    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, a_pause_queued_by_a_delay_0_entry_holds_that_entry) {
    TestGameModule::setConversationStepPending(*_game, true);
    startSilent(0);
    TestGameModule::setConversationStepPending(*_game, false);
    _conversation->updateSimulation(kTickSeconds);

    // Next tick: the speaker's queued ActionPauseConversation runs in the
    // world step, before the conversation step looks at the timer.
    _conversation->pause();
    for (int i = 0; i < 60; ++i) {
        _conversation->updateSimulation(kTickSeconds);
    }
    EXPECT_EQ("first", _conversation->currentText());
    EXPECT_EQ(0, _conversation->entryEndCount());

    _conversation->resume(); // ActionResumeConversation once the speaker arrives
    _conversation->updateSimulation(kTickSeconds);
    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, an_entry_started_between_ticks_keeps_its_timing) {
    // Started by a click or a console command: the next tick's world step
    // runs before its conversation step, so nothing is deferred.
    startSilent(0);
    _conversation->updateSimulation(kTickSeconds);
    EXPECT_EQ("second", _conversation->currentText());
}

TEST_F(ConversationTest, the_deferred_tick_does_not_count_towards_the_entry_s_delay) {
    TestGameModule::setConversationStepPending(*_game, true);
    startSilent(1);
    TestGameModule::setConversationStepPending(*_game, false);

    _conversation->updateSimulation(0.5f); // deferred: the entry has existed for no time
    _conversation->updateSimulation(0.6f);
    EXPECT_EQ("first", _conversation->currentText());
    _conversation->updateSimulation(0.4f);
    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, a_replica_mirrors_the_host_s_placeable_camera_cut) {
    auto dialog = makeDialog();
    dialog->entries[0].cameraAngle = 6;
    dialog->entries[0].cameraId = 20;
    _conversation->startReplica(dialog, nullptr);
    _conversation->showReplicaEntry(0, {0}, false);
    int cameraId = 0;
    ASSERT_EQ(CameraType::Static, _conversation->getCamera(cameraId));
    EXPECT_EQ(20, cameraId);

    // k_pend_ud_cut10 on the host: the cut hides the player's jump.
    _conversation->showReplicaEntry(0, {0}, false, 22);
    ASSERT_EQ(CameraType::Static, _conversation->getCamera(cameraId));
    EXPECT_EQ(22, cameraId);

    // The next entry's own camera, as on the host.
    _conversation->showReplicaEntry(1, {1}, false);
    EXPECT_EQ(CameraType::Dialog, _conversation->getCamera(cameraId));
}

// A voiced entry plays its line out: Delay 0 is no extra time, not a cut
// line (K1: 40 auto-advancing voiced entries with Delay 0, e.g. danm13
// bastila E9). The entry lasts the longer of its Delay and its voice.

TEST_F(ConversationTest, a_voiced_delay_0_entry_lasts_its_voice) {
    startVoiced(0);
    _conversation->updateSimulation(0.5f);
    EXPECT_EQ("first", _conversation->currentText());
    _conversation->updateSimulation(0.5f);
    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, a_delay_longer_than_the_voice_holds_the_entry) {
    startVoiced(2);
    _conversation->updateSimulation(1.5f);
    EXPECT_EQ("first", _conversation->currentText());
    _conversation->updateSimulation(0.5f);
    EXPECT_EQ("second", _conversation->currentText());
}

TEST_F(ConversationTest, a_click_still_skips_a_voiced_entry) {
    startVoiced(0, /*skippable=*/true);
    _conversation->handle(input::Event::newMouseButtonDown(
        input::MouseButtonEvent(input::MouseButton::Left, true, 1, 0, 0)));
    _conversation->updateSimulation(kTickSeconds);
    EXPECT_EQ("second", _conversation->currentText());
}

// An empty node whose script queues a pause is held, not passed straight on
// (tar08_davik081 E14: pause, fade out, move the party, resume).

namespace {

std::shared_ptr<Dialog> makeEmptyFirstEntryDialog() {
    auto dialog = makeDialog(-1);
    dialog->entries[0].text.clear();
    dialog->entries[0].cameraId = 0; // as a loaded DLG node without a camera
    return dialog;
}

} // namespace

TEST_F(ConversationTest, an_empty_entry_with_nothing_queued_passes_straight_on) {
    auto area = _game->newArea();
    TestGameModule::setActiveModuleArea(*_game, area);
    _conversation->start(makeEmptyFirstEntryDialog(), nullptr);
    EXPECT_EQ("second", _conversation->currentText());
}

TEST_F(ConversationTest, an_empty_entry_whose_script_queued_a_pause_is_held_until_the_resume) {
    auto area = _game->newArea();
    TestGameModule::setActiveModuleArea(*_game, area);
    auto owner = _game->newCreature();
    area->add(owner);
    owner->addAction(_game->newAction<PauseConversationAction>()); // as the node's script does
    _conversation->start(makeEmptyFirstEntryDialog(), nullptr);
    EXPECT_EQ("", _conversation->currentText());

    _conversation->pause(); // the owner's queued pause runs in the next world step
    _conversation->updateSimulation(kTickSeconds);
    _conversation->updateSimulation(1.0f);
    EXPECT_EQ("", _conversation->currentText());

    _conversation->resume();
    _conversation->updateSimulation(kTickSeconds);
    EXPECT_EQ("second", _conversation->currentText()); // no time of its own after the resume
}
