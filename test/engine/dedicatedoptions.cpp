/* Copyright (c) 2026 The reone project contributors */

// --dedicated through the command line and reone.cfg (docs/dedicated_host.md).

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "../../src/apps/engine/optionsparser.h"

using namespace reone;

namespace {

std::unique_ptr<Options> parseArgs(std::vector<std::string> args) {
    args.insert(args.begin(), "engine");
    std::vector<char *> argv;
    for (auto &arg : args) {
        argv.push_back(arg.data());
    }
    OptionsParser parser(static_cast<int>(argv.size()), argv.data());
    return parser.parse();
}

// Each test runs in a scratch directory, so any reone.cfg there is its own.
class DedicatedOptionsParse : public testing::Test {
protected:
    void SetUp() override {
        _previous = std::filesystem::current_path();
        _dir = std::filesystem::temp_directory_path() /
               (std::string("reone_dedopts_") + testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(_dir);
        std::filesystem::create_directories(_dir);
        std::filesystem::current_path(_dir);
    }

    void TearDown() override {
        std::filesystem::current_path(_previous);
        std::error_code ec;
        std::filesystem::remove_all(_dir, ec);
    }

    void writeConfig(const std::string &text) {
        std::ofstream(_dir / "reone.cfg") << text;
    }

private:
    std::filesystem::path _previous;
    std::filesystem::path _dir;
};

} // namespace

TEST_F(DedicatedOptionsParse, off_by_default_and_normal_hosting_is_unchanged) {
    auto options = parseArgs({"--host", "28000"});
    EXPECT_FALSE(options->dedicated.enabled);
    EXPECT_FALSE(options->graphics.headless);
    EXPECT_EQ(28000, options->coop.hostPort);
    EXPECT_EQ("story", options->coop.mode);
    EXPECT_TRUE(options->coop.password.empty());
}

TEST_F(DedicatedOptionsParse, dedicated_implies_headless_free_mode_and_no_sound) {
    auto options = parseArgs({"--dedicated"});
    EXPECT_TRUE(options->dedicated.enabled);
    EXPECT_TRUE(options->graphics.headless);
    EXPECT_TRUE(options->audio.muted);
    EXPECT_EQ("free", options->coop.mode);
    EXPECT_EQ(27015, options->dedicated.port);
    EXPECT_EQ(8, options->dedicated.maxPlayers);
    EXPECT_EQ(reone::game::kDedicatedDefaultSaveSlot, options->dedicated.saveSlot);
    EXPECT_EQ(300.0, options->dedicated.autosaveSeconds);
    EXPECT_TRUE(options->dedicated.adminStdin);
    // The dedicated host starts hosting itself; the engine must not also start a story host.
    EXPECT_EQ(0, options->coop.hostPort);
}

TEST_F(DedicatedOptionsParse, the_command_line_sets_everything) {
    auto options = parseArgs({"--dedicated", "--host", "28001", "--coop-max-players", "4", "--coop-password", "pw",
                              "--coop-server", "play.example.org:27200", "--dedicated-save-slot", "42",
                              "--dedicated-autosave", "90", "--dedicated-start-module", "end_m01aa", "--dedicated-stdin", "0"});
    const auto &d = options->dedicated;
    EXPECT_EQ(28001, d.port);
    EXPECT_EQ(4, d.maxPlayers);
    EXPECT_EQ("pw", d.password);
    EXPECT_EQ("play.example.org:27200", d.server);
    EXPECT_EQ(42u, d.saveSlot);
    EXPECT_EQ(90.0, d.autosaveSeconds);
    EXPECT_EQ("end_m01aa", d.startModule);
    EXPECT_FALSE(d.adminStdin);
}

TEST_F(DedicatedOptionsParse, reone_cfg_keys_work_too) {
    writeConfig("dedicated=true\ncoop-max-players=3\ncoop-password=cfgpw\ndedicated-autosave=0\nhost=29000\n");
    auto options = parseArgs({});
    EXPECT_TRUE(options->dedicated.enabled);
    EXPECT_EQ(3, options->dedicated.maxPlayers);
    EXPECT_EQ("cfgpw", options->dedicated.password);
    EXPECT_EQ(0.0, options->dedicated.autosaveSeconds);
    EXPECT_EQ(29000, options->dedicated.port);
}

TEST_F(DedicatedOptionsParse, bad_values_are_refused) {
    EXPECT_THROW(parseArgs({"--dedicated", "--coop-max-players", "9"}), std::invalid_argument);
    EXPECT_THROW(parseArgs({"--dedicated", "--coop-max-players", "0"}), std::invalid_argument);
    EXPECT_THROW(parseArgs({"--dedicated", "--dedicated-autosave=-5"}), std::invalid_argument);
    EXPECT_THROW(parseArgs({"--dedicated", "--dedicated-save-slot", "1"}), std::invalid_argument);
    EXPECT_THROW(parseArgs({"--dedicated", "--join", "127.0.0.1:27015"}), std::invalid_argument);
}

TEST_F(DedicatedOptionsParse, a_password_alone_applies_to_a_normal_host_and_to_a_guest) {
    auto host = parseArgs({"--host", "--coop-password", "tableword"});
    EXPECT_EQ("tableword", host->coop.password);
    auto guest = parseArgs({"--join", "127.0.0.1:27015", "--coop-password", "tableword"});
    EXPECT_EQ("tableword", guest->coop.password);
}
