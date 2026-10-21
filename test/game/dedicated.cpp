/* Copyright (c) 2026 The reone project contributors */

// The dedicated host's rules without a game (docs/dedicated_host.md): who
// drives the story, when it autosaves, how it resumes and shuts down, and the
// admin commands. The backend is a fake that records what it was asked.

#include <gtest/gtest.h>

#include <boost/json.hpp>

#include "reone/game/dedicated.h"

using namespace reone::game;

namespace {

class FakeBackend : public IDedicatedBackend {
public:
    bool startHost(const DedicatedSettings &settings, std::string &error) override {
        hosted = settings;
        ++hostStarts;
        if (failHost) {
            error = "port in use";
            return false;
        }
        return true;
    }
    bool saveExists(uint32_t slot) override { return savesPresent && slot == existingSlot; }
    bool loadSave(uint32_t slot, std::string &error) override {
        loadedSlot = static_cast<int64_t>(slot);
        if (failLoad) {
            error = "corrupt";
            return false;
        }
        return true;
    }
    bool startNewGame(const std::string &module, std::string &) override {
        newGameModule = module;
        ++newGames;
        return true;
    }
    bool worldReady() override { return ready; }
    SaveOutcome requestSave(uint32_t slot, const std::string &name) override {
        saveRequests.push_back({slot, name});
        if (saveOutcome == SaveOutcome::Accepted) {
            busy = true;
            busyFrames = holdSaveFrames;
        }
        return saveOutcome;
    }
    bool saveBusy() override {
        if (busy && busyFrames-- <= 0) {
            busy = false;
        }
        return busy;
    }
    bool lastSaveSucceeded() override { return saveOk; }
    std::vector<DedicatedPlayer> players() override { return roster; }
    bool kick(uint32_t id) override {
        for (auto it = roster.begin(); it != roster.end(); ++it) {
            if (it->id == id) {
                kicked.push_back(id);
                roster.erase(it);
                return true;
            }
        }
        return false;
    }
    uint32_t storyDriver() override { return roster.empty() ? 0 : roster.front().id; }
    std::string currentModule() override { return "test_module"; }
    void stopHost() override { ++hostStops; }

    DedicatedSettings hosted;
    bool failHost {false};
    bool failLoad {false};
    bool savesPresent {false};
    uint32_t existingSlot {kDedicatedDefaultSaveSlot};
    int64_t loadedSlot {-1};
    std::string newGameModule {"<none>"};
    int newGames {0};
    int hostStarts {0};
    int hostStops {0};
    bool ready {true};
    SaveOutcome saveOutcome {SaveOutcome::Accepted};
    bool saveOk {true};
    bool busy {false};
    int busyFrames {0};
    int holdSaveFrames {0};
    std::vector<std::pair<uint32_t, std::string>> saveRequests;
    std::vector<DedicatedPlayer> roster;
    std::vector<uint32_t> kicked;
};

DedicatedSettings settings() {
    DedicatedSettings result;
    result.enabled = true;
    result.autosaveSeconds = 60.0;
    return result;
}

void run(DedicatedHost &host, double seconds, double step = 1.0) {
    for (double t = 0.0; t < seconds; t += step) {
        host.update(step);
    }
}

class DedicatedTest : public testing::Test {
protected:
    void SetUp() override { clearDedicatedShutdown(); }
    void TearDown() override { clearDedicatedShutdown(); }
};

} // namespace

// Settings

TEST_F(DedicatedTest, settings_are_checked) {
    EXPECT_TRUE(validateDedicatedSettings(settings()).empty());
    auto bad = settings();
    bad.maxPlayers = 9;
    EXPECT_FALSE(validateDedicatedSettings(bad).empty());
    bad = settings();
    bad.maxPlayers = 0;
    EXPECT_FALSE(validateDedicatedSettings(bad).empty());
    bad = settings();
    bad.port = 0;
    EXPECT_FALSE(validateDedicatedSettings(bad).empty());
    bad = settings();
    bad.autosaveSeconds = -1.0;
    EXPECT_FALSE(validateDedicatedSettings(bad).empty());
    bad = settings();
    bad.saveSlot = 1; // the auto save slot, which the game itself writes
    EXPECT_FALSE(validateDedicatedSettings(bad).empty());
    bad = settings();
    bad.autosaveSeconds = 0.0; // off is allowed
    EXPECT_TRUE(validateDedicatedSettings(bad).empty());
}

// Leaderless rules

TEST_F(DedicatedTest, the_guest_longest_in_the_session_drives_the_story) {
    StoryDriverTracker tracker;
    EXPECT_EQ(0u, tracker.driver());
    EXPECT_TRUE(tracker.update({4}));
    EXPECT_EQ(4u, tracker.driver());
    // Later arrivals queue behind it, whatever order they are listed in.
    EXPECT_FALSE(tracker.update({6, 4, 5}));
    EXPECT_EQ(4u, tracker.driver());
    EXPECT_EQ((std::vector<uint32_t> {4, 5, 6}), tracker.line());
}

TEST_F(DedicatedTest, the_next_guest_takes_over_when_the_driver_leaves_and_a_returner_goes_to_the_back) {
    StoryDriverTracker tracker;
    tracker.update({1, 2, 3});
    EXPECT_TRUE(tracker.update({2, 3}));
    EXPECT_EQ(2u, tracker.driver());
    // Client 1 comes back (with whatever id the host gave it): behind the others.
    EXPECT_FALSE(tracker.update({1, 2, 3}));
    EXPECT_EQ(2u, tracker.driver());
    EXPECT_EQ((std::vector<uint32_t> {2, 3, 1}), tracker.line());
    EXPECT_TRUE(tracker.update({}));
    EXPECT_EQ(0u, tracker.driver());
}

TEST_F(DedicatedTest, a_conversation_nobody_answers_takes_its_first_reply_soon) {
    ConversationFallback fallback;
    // No driver: a few seconds, then it moves on, and the timer starts over.
    EXPECT_FALSE(fallback.update(2.0, true, false));
    EXPECT_TRUE(fallback.update(1.5, true, false));
    EXPECT_FALSE(fallback.update(1.0, true, false));
}

TEST_F(DedicatedTest, a_driver_gets_a_long_time_to_choose_and_a_missing_reply_list_resets_the_wait) {
    ConversationFallback fallback;
    EXPECT_FALSE(fallback.update(60.0, true, true));
    EXPECT_FALSE(fallback.update(10.0, false, true)); // the NPC is speaking: nothing to choose
    EXPECT_EQ(0.0, fallback.idle());
    EXPECT_FALSE(fallback.update(60.0, true, true));
    EXPECT_TRUE(fallback.update(31.0, true, true));
}

// Autosave scheduling

TEST_F(DedicatedTest, the_autosave_comes_due_after_its_interval_and_restarts_when_the_save_finishes) {
    AutosaveScheduler scheduler(60.0);
    scheduler.advance(59.0);
    EXPECT_FALSE(scheduler.due());
    scheduler.advance(1.0);
    EXPECT_TRUE(scheduler.due());
    scheduler.started();
    EXPECT_FALSE(scheduler.due());
    scheduler.advance(500.0); // the clock does not run while a save does
    scheduler.finished(true);
    EXPECT_FALSE(scheduler.due());
    EXPECT_EQ(60.0, scheduler.secondsUntilDue());
    EXPECT_EQ(1u, scheduler.saves());
}

TEST_F(DedicatedTest, a_refused_or_failed_autosave_is_tried_again_soon_not_after_a_whole_interval) {
    AutosaveScheduler scheduler(300.0, 10.0);
    scheduler.advance(300.0);
    ASSERT_TRUE(scheduler.due());
    scheduler.refused();
    EXPECT_FALSE(scheduler.due());
    scheduler.advance(10.0);
    EXPECT_TRUE(scheduler.due());
    scheduler.started();
    scheduler.finished(false);
    EXPECT_EQ(1u, scheduler.failures());
    scheduler.advance(10.0);
    EXPECT_TRUE(scheduler.due());
}

TEST_F(DedicatedTest, an_interval_of_zero_never_autosaves) {
    AutosaveScheduler scheduler(0.0);
    scheduler.advance(1.0e6);
    EXPECT_FALSE(scheduler.enabled());
    EXPECT_FALSE(scheduler.due());
}

// The controller

TEST_F(DedicatedTest, with_no_save_it_starts_a_new_game_in_the_chosen_module_and_hosts_free_mode_settings) {
    FakeBackend backend;
    auto config = settings();
    config.startModule = "end_m01aa";
    config.port = 28000;
    config.maxPlayers = 4;
    config.password = "hunter2";
    DedicatedHost host(config, backend);
    host.update(0.1);
    EXPECT_EQ(DedicatedState::Loading, host.state());
    EXPECT_EQ(28000, backend.hosted.port);
    EXPECT_EQ(4, backend.hosted.maxPlayers);
    EXPECT_EQ("hunter2", backend.hosted.password);
    EXPECT_EQ(1, backend.newGames);
    EXPECT_EQ("end_m01aa", backend.newGameModule);
    EXPECT_EQ(-1, backend.loadedSlot);
    host.update(0.1);
    EXPECT_EQ(DedicatedState::Running, host.state());
}

TEST_F(DedicatedTest, it_resumes_from_the_save_slot_when_there_is_one) {
    FakeBackend backend;
    backend.savesPresent = true;
    auto config = settings();
    config.saveSlot = 1234;
    backend.existingSlot = 1234;
    DedicatedHost host(config, backend);
    host.update(0.1);
    EXPECT_EQ(1234, backend.loadedSlot);
    EXPECT_EQ(0, backend.newGames);
    host.update(0.1);
    EXPECT_EQ(DedicatedState::Running, host.state());
    EXPECT_NE(std::string::npos, host.statusJson().find("\"resumed\":true"));
}

TEST_F(DedicatedTest, a_port_in_use_or_a_bad_save_fails_with_a_reason) {
    {
        FakeBackend backend;
        backend.failHost = true;
        DedicatedHost host(settings(), backend);
        host.update(0.1);
        EXPECT_TRUE(host.failed());
        EXPECT_TRUE(host.finished());
        EXPECT_NE(std::string::npos, host.error().find("port in use"));
    }
    {
        FakeBackend backend;
        backend.savesPresent = true;
        backend.failLoad = true;
        DedicatedHost host(settings(), backend);
        host.update(0.1);
        EXPECT_TRUE(host.failed());
        EXPECT_NE(std::string::npos, host.error().find("corrupt"));
    }
    {
        FakeBackend backend;
        auto config = settings();
        config.maxPlayers = 12;
        DedicatedHost host(config, backend);
        host.update(0.1);
        EXPECT_TRUE(host.failed());
        EXPECT_EQ(0, backend.hostStarts); // refused before anything was opened
    }
}

TEST_F(DedicatedTest, it_waits_for_the_world_and_gives_up_if_it_never_comes) {
    FakeBackend backend;
    backend.ready = false;
    DedicatedHost host(settings(), backend);
    host.update(0.1);
    host.update(100.0);
    EXPECT_EQ(DedicatedState::Loading, host.state());
    backend.ready = true;
    host.update(0.1);
    EXPECT_EQ(DedicatedState::Running, host.state());

    FakeBackend stuck;
    stuck.ready = false;
    DedicatedHost never(settings(), stuck);
    never.update(0.1);
    never.update(DedicatedHost::kLoadTimeoutSeconds + 1.0);
    EXPECT_TRUE(never.failed());
}

TEST_F(DedicatedTest, it_autosaves_on_the_interval_to_its_slot) {
    FakeBackend backend;
    auto config = settings();
    config.saveSlot = 777;
    DedicatedHost host(config, backend);
    run(host, 2.0); // boot, load, run
    ASSERT_EQ(DedicatedState::Running, host.state());
    EXPECT_TRUE(backend.saveRequests.empty());
    run(host, 61.0);
    ASSERT_EQ(1u, backend.saveRequests.size());
    EXPECT_EQ(777u, backend.saveRequests[0].first);
    // Not again until another interval has passed.
    run(host, 50.0);
    EXPECT_EQ(1u, backend.saveRequests.size());
    run(host, 20.0);
    EXPECT_EQ(2u, backend.saveRequests.size());
    EXPECT_EQ(2u, host.autosave().saves());
}

TEST_F(DedicatedTest, an_autosave_the_game_refuses_is_retried_shortly) {
    FakeBackend backend;
    backend.saveOutcome = SaveOutcome::NotAllowed; // mid-transition
    DedicatedHost host(settings(), backend);
    run(host, 62.0);
    EXPECT_EQ(1u, backend.saveRequests.size());
    run(host, 11.0);
    EXPECT_EQ(2u, backend.saveRequests.size());
    backend.saveOutcome = SaveOutcome::Accepted;
    run(host, 10.0);
    EXPECT_EQ(3u, backend.saveRequests.size());
}

TEST_F(DedicatedTest, a_failed_autosave_is_counted_and_does_not_stop_the_host) {
    FakeBackend backend;
    backend.saveOk = false;
    DedicatedHost host(settings(), backend);
    run(host, 64.0);
    EXPECT_EQ(DedicatedState::Running, host.state());
    EXPECT_EQ(1u, host.autosave().failures());
}

TEST_F(DedicatedTest, a_signal_saves_then_stops_hosting_and_finishes) {
    FakeBackend backend;
    backend.holdSaveFrames = 3;
    DedicatedHost host(settings(), backend);
    run(host, 3.0);
    ASSERT_EQ(DedicatedState::Running, host.state());
    requestDedicatedShutdown(15);
    host.update(0.1);
    EXPECT_EQ(DedicatedState::ShuttingDown, host.state());
    EXPECT_FALSE(host.finished());
    for (int i = 0; i < 20 && !host.finished(); ++i) {
        host.update(0.1);
    }
    ASSERT_TRUE(host.finished());
    EXPECT_FALSE(host.failed());
    ASSERT_EQ(1u, backend.saveRequests.size());
    EXPECT_EQ("Dedicated shutdown", backend.saveRequests[0].second);
    EXPECT_EQ(1, backend.hostStops);
}

TEST_F(DedicatedTest, shutdown_waits_for_a_save_already_running_before_taking_its_own) {
    FakeBackend backend;
    backend.holdSaveFrames = 5;
    DedicatedHost host(settings(), backend);
    run(host, 62.0); // an autosave starts and is still running
    ASSERT_TRUE(host.autosave().saving());
    host.shutdown();
    for (int i = 0; i < 50 && !host.finished(); ++i) {
        host.update(0.1);
    }
    EXPECT_TRUE(host.finished());
    ASSERT_EQ(2u, backend.saveRequests.size());
    EXPECT_EQ("Dedicated shutdown", backend.saveRequests[1].second);
}

TEST_F(DedicatedTest, a_shutdown_that_cannot_save_still_stops_hosting) {
    FakeBackend backend;
    backend.saveOutcome = SaveOutcome::NotAllowed;
    DedicatedHost host(settings(), backend);
    run(host, 3.0);
    host.shutdown();
    for (int i = 0; i < 10 && !host.finished(); ++i) {
        host.update(0.1);
    }
    EXPECT_TRUE(host.finished());
    EXPECT_EQ(1, backend.hostStops);
}

TEST_F(DedicatedTest, shutting_down_while_still_loading_does_not_try_to_save) {
    FakeBackend backend;
    backend.ready = false;
    DedicatedHost host(settings(), backend);
    host.update(0.1);
    requestDedicatedShutdown(2);
    host.update(0.1);
    EXPECT_TRUE(host.finished());
    EXPECT_TRUE(backend.saveRequests.empty());
    EXPECT_EQ(1, backend.hostStops);
}

TEST_F(DedicatedTest, the_signal_flag_remembers_the_signal) {
    EXPECT_FALSE(dedicatedShutdownRequested());
    requestDedicatedShutdown(15);
    EXPECT_TRUE(dedicatedShutdownRequested());
    EXPECT_EQ(15, dedicatedShutdownSignal());
    clearDedicatedShutdown();
    EXPECT_FALSE(dedicatedShutdownRequested());
}

// Admin commands

namespace {

boost::json::object parse(const AdminReply &reply) {
    EXPECT_EQ(1u, reply.lines.size());
    return boost::json::parse(reply.lines.at(0)).as_object();
}

} // namespace

class DedicatedAdminTest : public DedicatedTest {
protected:
    void SetUp() override {
        DedicatedTest::SetUp();
        backend.roster = {{3, "Alice", "", false, false}, {5, "Bob Smith", "", true, false}};
        host = std::make_unique<DedicatedHost>(settings(), backend);
        run(*host, 3.0);
    }

    FakeBackend backend;
    std::unique_ptr<DedicatedHost> host;
};

TEST_F(DedicatedAdminTest, players_lists_everyone_and_marks_the_story_driver) {
    auto reply = executeDedicatedAdmin(*host, "players");
    ASSERT_TRUE(reply.ok);
    auto json = parse(reply);
    EXPECT_EQ(2, json.at("count").as_int64());
    const auto &players = json.at("players").as_array();
    EXPECT_EQ("Alice", players[0].at("name").as_string());
    EXPECT_TRUE(players[0].at("storyDriver").as_bool());
    EXPECT_FALSE(players[1].at("storyDriver").as_bool());
    EXPECT_TRUE(players[1].at("waiting").as_bool());
}

TEST_F(DedicatedAdminTest, kick_takes_an_id_or_a_name) {
    auto byId = executeDedicatedAdmin(*host, "kick 3");
    EXPECT_TRUE(byId.ok);
    EXPECT_EQ((std::vector<uint32_t> {3}), backend.kicked);
    auto byName = executeDedicatedAdmin(*host, "kick bob smith");
    EXPECT_TRUE(byName.ok);
    EXPECT_EQ((std::vector<uint32_t> {3, 5}), backend.kicked);
}

TEST_F(DedicatedAdminTest, kick_of_nobody_or_nothing_fails_without_kicking) {
    EXPECT_FALSE(executeDedicatedAdmin(*host, "kick 99").ok);
    EXPECT_FALSE(executeDedicatedAdmin(*host, "kick Nobody").ok);
    EXPECT_FALSE(executeDedicatedAdmin(*host, "kick").ok);
    EXPECT_TRUE(backend.kicked.empty());
}

TEST_F(DedicatedAdminTest, save_takes_a_save_now_and_refuses_a_second_while_it_runs) {
    backend.holdSaveFrames = 2;
    auto first = executeDedicatedAdmin(*host, "save");
    ASSERT_TRUE(first.ok);
    EXPECT_EQ(static_cast<int64_t>(settings().saveSlot), parse(first).at("slot").as_int64());
    ASSERT_EQ(1u, backend.saveRequests.size());
    EXPECT_EQ("Dedicated save", backend.saveRequests[0].second);
    EXPECT_FALSE(executeDedicatedAdmin(*host, "save").ok);
    run(*host, 5.0, 0.5);
    EXPECT_TRUE(executeDedicatedAdmin(*host, "save").ok);
}

TEST_F(DedicatedAdminTest, a_save_the_game_refuses_is_reported) {
    backend.saveOutcome = SaveOutcome::NotAllowed;
    auto reply = executeDedicatedAdmin(*host, "save");
    EXPECT_FALSE(reply.ok);
    EXPECT_NE(std::string::npos, reply.lines[0].find("not at a point"));
}

TEST_F(DedicatedAdminTest, shutdown_and_its_aliases_start_the_clean_stop) {
    for (const char *verb : {"shutdown", "stop", "quit", "exit"}) {
        FakeBackend other;
        DedicatedHost second(settings(), other);
        run(second, 3.0);
        auto reply = executeDedicatedAdmin(second, verb);
        EXPECT_TRUE(reply.ok) << verb;
        EXPECT_TRUE(second.shuttingDown()) << verb;
    }
}

TEST_F(DedicatedAdminTest, the_control_port_form_is_the_same_command) {
    // `dedicated players` on the control port, case and spacing aside.
    EXPECT_TRUE(executeDedicatedAdmin(*host, "dedicated players").ok);
    EXPECT_TRUE(executeDedicatedAdmin(*host, "  Dedicated   KICK 3 ").ok);
    EXPECT_EQ((std::vector<uint32_t> {3}), backend.kicked);
}

TEST_F(DedicatedAdminTest, status_help_and_unknown_commands) {
    auto status = parse(executeDedicatedAdmin(*host, "status"));
    EXPECT_EQ("running", status.at("state").as_string());
    EXPECT_EQ(2, status.at("players").as_int64());
    EXPECT_EQ(8, status.at("maxPlayers").as_int64());
    EXPECT_EQ(3, status.at("storyDriver").as_int64());
    EXPECT_TRUE(status.at("autosave").as_object().at("enabled").as_bool());
    EXPECT_TRUE(executeDedicatedAdmin(*host, "help").ok);
    EXPECT_FALSE(executeDedicatedAdmin(*host, "frobnicate").ok);
    EXPECT_FALSE(executeDedicatedAdmin(*host, "").ok);
}

TEST_F(DedicatedAdminTest, the_password_is_never_in_a_reply) {
    DedicatedSettings config = settings();
    config.password = "s3cret-word";
    FakeBackend other;
    DedicatedHost second(config, other);
    run(second, 3.0);
    auto status = executeDedicatedAdmin(second, "status");
    EXPECT_EQ(std::string::npos, status.lines[0].find("s3cret-word"));
    EXPECT_NE(std::string::npos, status.lines[0].find("\"passwordSet\":true"));
}

TEST_F(DedicatedAdminTest, commands_that_need_a_running_world_say_so_before_it_is_up) {
    FakeBackend other;
    other.ready = false;
    DedicatedHost second(settings(), other);
    second.update(0.1);
    EXPECT_FALSE(executeDedicatedAdmin(second, "save").ok);
}
