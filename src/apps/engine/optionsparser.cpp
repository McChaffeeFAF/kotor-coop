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

#include "optionsparser.h"

#include <cmath>
#include <cstdlib>

#include "reone/graphics/preset.h"
#include "reone/net/joincode.h"
#include "reone/system/gamedetect.h"
#include "reone/system/types.h"

using namespace boost::program_options;

using namespace reone::game;
using namespace reone::graphics;

namespace reone {

static constexpr char kConfigFilename[] = "reone.cfg";

static float positiveFiniteScale(const variables_map &vars, const char *name) {
    float value = vars[name].as<float>();
    if (!std::isfinite(value) || value <= 0.0f) {
        throw std::invalid_argument(std::string("--") + name + " must be finite and greater than zero");
    }
    return value;
}

// "1/144" or "0.0069": a positive, finite number of seconds.
static float parseFrameTime(const std::string &text) {
    float value = 0.0f;
    try {
        auto slash = text.find('/');
        if (slash != std::string::npos) {
            float numerator = std::stof(text.substr(0, slash));
            float denominator = std::stof(text.substr(slash + 1));
            value = denominator != 0.0f ? numerator / denominator : 0.0f;
        } else {
            value = std::stof(text);
        }
    } catch (const std::exception &) {
        value = 0.0f;
    }
    if (!std::isfinite(value) || value <= 0.0f || value > 1.0f) {
        throw std::invalid_argument("headless-frametime must be a number of seconds in (0, 1], e.g. 1/144");
    }
    return value;
}

/**
 * Steam Deck: the 1280x800 panel, fullscreen, and a larger GUI so text is readable at handheld
 * distance. These are defaults only; --width, --height, --fullscreen, --guiscale and reone.cfg
 * override them.
 */
static void applySteamDeckDefaults(Options &options) {
    const char *user = std::getenv("USER");
    if (!isSteamDeck(std::getenv("SteamDeck"), user ? user : "")) {
        return;
    }
    options.graphics.width = 1280;
    options.graphics.height = 800;
    options.graphics.fullscreen = true;
    options.graphics.guiScale = 1.25f;
    options.graphics.guiTextScale *= 1.25f;
}

/** The first KotOR install found in the usual Linux places, or empty. */
static std::filesystem::path detectLinuxInstall() {
#ifdef __linux__
    const char *home = std::getenv("HOME");
    if (!home || !*home) {
        return {};
    }
    const char *user = std::getenv("USER");
    auto installs = findGameInstalls(home, KotorGame::K1, defaultMounts(user ? user : ""));
    if (!installs.empty()) {
        return installs.front().path;
    }
#endif
    return {};
}

std::unique_ptr<Options> OptionsParser::parse() {
    auto options = std::make_unique<Options>();
    applySteamDeckDefaults(*options);

    int defaultLogChannels = 0;
    for (auto &channel : options->logging.channels) {
        defaultLogChannels |= static_cast<int>(channel);
    }

    // Initialize options description

    options_description descCommon;
    descCommon.add_options()                                                                                                    //
        ("game", value<std::string>(), "path to game directory")                                                                //
        ("k2game", value<std::string>()->default_value(""), "path to a K2 install: its GUI resources (--hud k2) and the K2 systems merge") //
        ("k2merge", value<bool>()->default_value(true), "with --k2game, merge K2's feats, powers, classes, forms, crafting and the level-50 table into K1's data; 0 keeps K1 data only") //
        ("hud", value<std::string>()->default_value("k1"), "HUD presentation: k1 (default) or k2; empty follows the game")                //
        ("commands-file", value<std::string>()->default_value(""), "execute console commands from a file at startup")           //
        ("control-port", value<int>()->default_value(0), "accept console commands on 127.0.0.1 at this port; 0 disables")       //
        ("tickrate", value<int>()->default_value(options->game.tickRate), "fixed simulation rate, ticks per second")             //
        ("regen-rate", value<float>()->default_value(options->game.regenRate), "natural regeneration speed out of combat; 1 is retail") //
        ("interpolate", value<bool>()->default_value(options->game.interpolate), "draw between simulation ticks (smooth motion above the tick rate); 0 draws each tick as is") //
        ("tick-gate", value<bool>()->default_value(false), "start with the tick gate closed: ticks run only when granted")       //
        ("immortal-player", value<bool>()->default_value(false)->implicit_value(true), "the player character cannot die (developer)") //
        ("friendly-collision-off", value<bool>()->default_value(false)->implicit_value(true), "party members and player characters pass through each other (co-op friendly collision off)") //
        ("pause-in-menus", value<bool>()->default_value(false)->implicit_value(true), "pause the world in the in-game menus and container screens, as retail does") //
        ("headless-frametime", value<std::string>()->default_value("1/60"), "seconds per headless frame, e.g. 1/144 or 0.01")    //
        ("host", value<int>()->default_value(0)->implicit_value(27015), "host a co-op session on this UDP port (27015 without a number); with --coop-server it also gets a join code; networking is off without --host or --join") //
        ("join", value<std::string>()->default_value(""), "join a co-op session by join code (needs --coop-server) or at ip:port (port 27015 if omitted)") //
        ("coop-server", value<std::string>()->default_value(""), "co-op join-code service, host[:port] (default port 27200)") //
        ("coop-relay-only", value<bool>()->default_value(false)->implicit_value(true), "co-op by code: connect through the TURN relay only (tests)") //
        ("coop-ask-join", value<int>()->default_value(-1), "co-op host: 1 asks Accept/Decline for every new player, 0 never; default asks unless headless") //
        ("coop-sim-lag", value<int>()->default_value(0), "co-op test: milliseconds of lag added to everything this engine sends") //
        ("coop-sim-loss", value<float>()->default_value(0.0f), "co-op test: percent of packets this engine drops when sending") //
        ("coop-sim-jitter", value<int>()->default_value(0), "co-op test: milliseconds of random extra delay on sends") //
        ("coop-record", value<std::string>()->default_value(""), "write the game and co-op state once a second to this JSON-lines file (bug reports)") //
        ("coop-password", value<std::string>()->default_value(""), "co-op: a host requires this password of every guest, a guest sends it (empty: none)") //
        ("coop-max-players", value<int>()->default_value(game::kDedicatedMaxPlayers), "dedicated host: how many guests it takes, 1 to 8") //
        ("dedicated", value<bool>()->default_value(false)->implicit_value(true), "run a dedicated co-op host: Free Mode with no local player, no window or audio; the port is --host's (default 27015)") //
        ("dedicated-save-slot", value<int>()->default_value(static_cast<int>(game::kDedicatedDefaultSaveSlot)), "dedicated host: the save slot it resumes from and autosaves to") //
        ("dedicated-autosave", value<float>()->default_value(static_cast<float>(game::kDedicatedDefaultAutosaveSeconds)), "dedicated host: seconds between autosaves, 0 for none (it still saves on shutdown)") //
        ("dedicated-start-module", value<std::string>()->default_value(""), "dedicated host: with no save, the module a new game starts in (default: the game's first)") //
        ("dedicated-stdin", value<bool>()->default_value(true), "dedicated host: read admin commands (players, kick, save, shutdown) from standard input") //
        ("coop-mode", value<std::string>()->default_value("story"), "co-op session mode: story")                              //
        ("coop-name", value<std::string>()->default_value(""), "co-op player name")                                            //
        ("coop-slot", value<std::string>()->default_value(""), "co-op client: the companion tag to ask for; empty for any")    //
        ("coop-host-only-transitions", value<bool>()->default_value(false)->implicit_value(true), "co-op host: only the host's exit moves the party (no ready check)") //
        ("coop-host-only-trading", value<bool>()->default_value(false)->implicit_value(true), "co-op host: only the host may open Story Mode stores") //
        ("coop-no-pause", value<bool>()->default_value(false)->implicit_value(true), "co-op host: nobody can pause (fully real-time)") //
        ("coop-pause-cooldown", value<float>()->default_value(0.0f), "co-op host: seconds a player waits between pauses") //
        ("coop-enemy-scaling", value<float>()->default_value(1.0f), "co-op host: enemy HP/damage scaling per extra player; 1 is the design curve, 0 off") //
        ("dev", value<bool>()->default_value(options->game.developer), "enable developer mode")                                 //
        ("smart-companions", value<bool>()->default_value(false)->implicit_value(true), "Smarter companions: an engine-side brain fights for AI-controlled party members on the generic combat scripts (and for the player under auto-combat)") //
        ("autocombat", value<bool>()->default_value(false)->implicit_value(true), "the player character starts with auto-combat on (the AI fights for it; any input overrides)") //
        ("width", value<int>()->default_value(options->graphics.width), "render width")                                         //
        ("height", value<int>()->default_value(options->graphics.height), "render height")                                      //
        ("winscale", value<int>()->default_value(options->graphics.winScale), "window scale")                                   //
        ("fullscreen", value<bool>()->default_value(options->graphics.fullscreen), "enable fullscreen")                         //
        ("borderless", value<bool>()->default_value(true), "fullscreen windowed: a borderless window over the desktop (overrides fullscreen; set width and height to the desktop resolution)") //
        ("headless", value<bool>()->default_value(false), "never show the window; for scripted batch runs")                     //
        ("run-unfocused", value<bool>()->default_value(false), "keep running while the window is not focused")                  //
        ("vsync", value<bool>()->default_value(options->graphics.vsync), "enable v-sync")                                       //
        ("grass", value<bool>()->default_value(options->graphics.grass), "enable grass")                                        //
        ("shadows", value<bool>()->default_value(options->graphics.shadows), "enable shadows")                                  //
        ("particles", value<bool>()->default_value(options->graphics.particles), "enable emitter particles")                    //
        ("pbr", value<bool>()->default_value(options->graphics.pbr), "enable physically-based rendering")                       //
        ("ssao", value<bool>()->default_value(options->graphics.ssao), "enable screen-space ambient occlusion")                 //
        ("ssr", value<bool>()->default_value(options->graphics.ssr), "enable screen-space reflections")                         //
        ("fxaa", value<bool>()->default_value(options->graphics.fxaa), "enable anti-aliasing")                                  //
        ("sharpen", value<bool>()->default_value(options->graphics.sharpen), "enable image sharpening")                         //
        ("enhanced", value<bool>()->default_value(false), "Enhanced graphics preset: bloom, SMAA, soft shadows, GTAO, ACES tone mapping, eye adaptation, soft particles, reflections, lightsaber glow and conversation depth of field; flags given with it override its values (shadowres and anisofilter are separate)") //
        ("bloom", value<bool>()->default_value(options->graphics.bloom), "enable bloom (bright parts and the glow channel)") //
        ("bloom-intensity", value<float>()->default_value(options->graphics.bloomIntensity), "bloom intensity") //
        ("smaa", value<bool>()->default_value(options->graphics.smaa), "enable SMAA anti-aliasing (over FXAA)")       //
        ("taa", value<bool>()->default_value(options->graphics.taa), "enable temporal anti-aliasing (over SMAA and FXAA)") //
        ("aa", value<std::string>()->default_value(""), "anti-aliasing: none, fxaa, smaa or taa (overrides fxaa, smaa and taa)") //
        ("softshadows", value<bool>()->default_value(options->graphics.softShadows), "enable soft shadows (PCSS)") //
        ("gtao", value<bool>()->default_value(options->graphics.gtao), "enable ground-truth ambient occlusion")       //
        ("tonemap-op", value<std::string>()->default_value("aces"), "tone-mapping curve with --tonemap: aces or agx") //
        ("sharpen-amount", value<float>()->default_value(options->graphics.sharpenAmount), "image sharpening amount") //
        ("normalmap-strength", value<float>()->default_value(options->graphics.normalMapStrength), "surface detail strength, 0 to 1") //
        ("normalmap-folder", value<std::string>()->default_value(""), "surface detail: folder of generated <texture>_n / _m maps (.dds, or .tga; a .dds wins); default enhanced_textures beside the executable") //
        ("normalmap-test", value<bool>()->default_value(false), "surface detail: procedural test patterns instead of the folder (development)") //
        ("texquality", value<int>()->default_value(static_cast<int>(options->graphics.textureQuality)), "texture quality")      //
        ("shadowres", value<int>()->default_value(glm::log2(options->graphics.shadowResolution) - 10), "shadow map resolution, 1 << (10 + n): 1 is 2048 (default), 2 is 4096") //
        ("anisofilter", value<int>()->default_value(options->graphics.anisotropicFiltering), "anisotropic filtering, log2: 0 off, 2 is 4x (default), 4 is 16x") //
        ("drawdist", value<int>()->default_value(static_cast<int>(kDefaultObjectDrawDistance)), "draw distance")                //
        ("guiscale", value<float>()->default_value(options->graphics.guiScale), "GUI layout scale")                              //
        ("guitextscale", value<float>()->default_value(options->graphics.guiTextScale), "GUI text scale")                       //
        ("guidialogtextscale", value<float>()->default_value(options->graphics.guiDialogTextScale), "dialog text scale")       //
        ("guiborderscale", value<float>()->default_value(options->graphics.guiBorderScale), "GUI border scale")                 //
        ("guilistscale", value<float>()->default_value(options->graphics.guiListScale), "GUI list row scale")                    //
        ("musicvol", value<int>()->default_value(options->audio.musicVolume), "music volume in percents")                       //
        ("voicevol", value<int>()->default_value(options->audio.voiceVolume), "voice volume in percents")                       //
        ("soundvol", value<int>()->default_value(options->audio.soundVolume), "sound volume in percents")                       //
        ("movievol", value<int>()->default_value(options->audio.movieVolume), "movie volume in percents")                       //
        ("logsev", value<int>()->default_value(static_cast<int>(options->logging.severity)), "minimum log severity")            //
        ("logch", value<int>()->default_value(defaultLogChannels), "log channel mask")                                            //
        ("renderdiag", value<bool>()->default_value(true), "log render diagnostics (once per problem) whatever the channel mask");

    // Tier 2 features: one boolean key each (graphicsFeatureToggles).
    for (const auto &toggle : graphicsFeatureToggles()) {
        descCommon.add_options()(toggle.name, value<bool>()->default_value(options->graphics.*toggle.field), toggle.help);
    }

    options_description descCmdLine {"Usage"};
    descCmdLine.add(descCommon);
    // Command line only: reone.cfg is read with descCommon, which has no help.
    descCmdLine.add_options()("help,h", "print this list of options and exit");

    // Parse command line and configuration file

    variables_map vars;
    store(parse_command_line(_argc, _argv, descCmdLine), vars);
    if (vars.count("help") > 0) {
        std::cout << descCmdLine << std::endl;
        throw HelpRequested();
    }
    if (std::filesystem::exists(kConfigFilename)) {
        store(parse_config_file<char>(kConfigFilename, descCommon, true), vars);
    }
    notify(vars);

    // Convert Boost options to game options

    if (vars.count("game") > 0) {
        options->game.path = std::filesystem::path(vars["game"].as<std::string>());
    } else {
        // No --game: the current folder if it is an install, else the Steam/GOG install on Linux.
        options->game.path = std::filesystem::current_path();
        if (!isGameInstall(options->game.path, KotorGame::K1) && !isGameInstall(options->game.path, KotorGame::K2)) {
            if (auto detected = detectLinuxInstall(); !detected.empty()) {
                options->game.path = detected;
            }
        }
    }
    options->game.developer = vars["dev"].as<bool>();
    options->game.autoCombat = vars["autocombat"].as<bool>();
    options->game.smartCompanions = vars["smart-companions"].as<bool>();
    if (auto k2game = vars["k2game"].as<std::string>(); !k2game.empty()) {
        options->game.k2Path = std::filesystem::path(k2game);
    }
    options->game.k2Merge = vars["k2merge"].as<bool>();
    options->game.hud = game::parseHudProfile(vars["hud"].as<std::string>());
    options->game.configurationPath = std::filesystem::absolute(kConfigFilename);
    options->game.menuPresentation = game::MenuPresentation::load(options->game.configurationPath);
    options->game.feedback = game::FeedbackOptions::load(options->game.configurationPath);
    options->game.autoPause = game::AutoPauseOptions::load(options->game.configurationPath, false);
    options->graphics.width = vars["width"].as<int>();
    options->graphics.height = vars["height"].as<int>();
    options->graphics.winScale = vars["winscale"].as<int>();
    options->graphics.fullscreen = vars["fullscreen"].as<bool>();
    options->graphics.borderless = vars["borderless"].as<bool>();
    options->graphics.headless = vars["headless"].as<bool>();
    options->graphics.vsync = vars["vsync"].as<bool>();
    options->graphics.grass = vars["grass"].as<bool>();
    options->graphics.shadows = vars["shadows"].as<bool>();
    options->graphics.particles = vars["particles"].as<bool>();
    options->graphics.pbr = vars["pbr"].as<bool>();
    options->graphics.ssao = vars["ssao"].as<bool>();
    options->graphics.ssr = vars["ssr"].as<bool>();
    options->graphics.fxaa = vars["fxaa"].as<bool>();
    options->graphics.sharpen = vars["sharpen"].as<bool>();
    options->graphics.textureQuality = static_cast<TextureQuality>(vars["texquality"].as<int>());
    options->graphics.shadowResolution = 1 << (10 + vars["shadowres"].as<int>());
    options->graphics.anisotropicFiltering = vars["anisofilter"].as<int>();
    options->graphics.drawDistance = static_cast<float>(vars["drawdist"].as<int>());
    options->graphics.bloom = vars["bloom"].as<bool>();
    options->graphics.bloomIntensity = vars["bloom-intensity"].as<float>();
    options->graphics.smaa = vars["smaa"].as<bool>();
    options->graphics.taa = vars["taa"].as<bool>();
    options->graphics.softShadows = vars["softshadows"].as<bool>();
    options->graphics.gtao = vars["gtao"].as<bool>();
    options->graphics.sharpenAmount = vars["sharpen-amount"].as<float>();
    options->graphics.normalMapStrength = vars["normalmap-strength"].as<float>();
    options->graphics.normalMapFolder = vars["normalmap-folder"].as<std::string>();
    options->graphics.normalMapTest = vars["normalmap-test"].as<bool>();
    for (const auto &toggle : graphicsFeatureToggles()) {
        options->graphics.*toggle.field = vars[toggle.name].as<bool>();
    }
    if (vars["enhanced"].as<bool>()) {
        // The preset, then whatever was given explicitly on top of it.
        auto explicitOptions = options->graphics;
        applyGraphicsPreset(options->graphics, GraphicsPreset::Enhanced);
        auto given = [&vars](const char *name) {
            return vars.count(name) > 0 && !vars[name].defaulted();
        };
        if (given("bloom")) options->graphics.bloom = explicitOptions.bloom;
        if (given("fxaa")) options->graphics.fxaa = explicitOptions.fxaa;
        if (given("smaa")) options->graphics.smaa = explicitOptions.smaa;
        if (given("taa")) options->graphics.taa = explicitOptions.taa;
        if (given("softshadows")) options->graphics.softShadows = explicitOptions.softShadows;
        if (given("gtao")) options->graphics.gtao = explicitOptions.gtao;
        if (given("sharpen")) options->graphics.sharpen = explicitOptions.sharpen;
        if (given("sharpen-amount")) options->graphics.sharpenAmount = explicitOptions.sharpenAmount;
        for (const auto &toggle : graphicsFeatureToggles()) {
            if (given(toggle.name)) options->graphics.*toggle.field = explicitOptions.*toggle.field;
        }
    }
    if (auto op = parseToneMapOperator(vars["tonemap-op"].as<std::string>())) {
        options->graphics.tonemapOperator = *op;
    } else {
        throw std::invalid_argument("tonemap-op must be aces or agx");
    }
    if (auto aa = vars["aa"].as<std::string>(); !aa.empty()) {
        auto mode = parseAntiAliasing(aa);
        if (!mode) {
            throw std::invalid_argument("aa must be none, fxaa, smaa or taa");
        }
        setAntiAliasing(options->graphics, *mode);
    }
    if (!std::isfinite(options->graphics.bloomIntensity) || options->graphics.bloomIntensity < 0.0f ||
        !std::isfinite(options->graphics.sharpenAmount) || options->graphics.sharpenAmount < 0.0f) {
        throw std::invalid_argument("bloom-intensity and sharpen-amount must be finite and not negative");
    }
    if (!std::isfinite(options->graphics.normalMapStrength) || options->graphics.normalMapStrength < 0.0f ||
        options->graphics.normalMapStrength > 1.0f) {
        throw std::invalid_argument("normalmap-strength must be between 0 and 1");
    }
    options->graphics.guiScale = positiveFiniteScale(vars, "guiscale");
    options->graphics.guiTextScale = positiveFiniteScale(vars, "guitextscale");
    options->graphics.guiDialogTextScale = positiveFiniteScale(vars, "guidialogtextscale");
    options->graphics.guiBorderScale = positiveFiniteScale(vars, "guiborderscale");
    options->graphics.guiListScale = positiveFiniteScale(vars, "guilistscale");
    options->audio.musicVolume = vars["musicvol"].as<int>();
    options->audio.voiceVolume = vars["voicevol"].as<int>();
    options->audio.soundVolume = vars["soundvol"].as<int>();
    options->audio.movieVolume = vars["movievol"].as<int>();
    options->logging.severity = static_cast<LogSeverity>(vars["logsev"].as<int>());

    std::set<LogChannel> logChannels;
    int logChannelsMask = vars["logch"].as<int>();
    if ((logChannelsMask & static_cast<int>(LogChannel::Global)) != 0) {
        logChannels.insert(LogChannel::Global);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Resources)) != 0) {
        logChannels.insert(LogChannel::Resources);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Resources2)) != 0) {
        logChannels.insert(LogChannel::Resources2);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Graphics)) != 0) {
        logChannels.insert(LogChannel::Graphics);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Audio)) != 0) {
        logChannels.insert(LogChannel::Audio);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::GUI)) != 0) {
        logChannels.insert(LogChannel::GUI);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Perception)) != 0) {
        logChannels.insert(LogChannel::Perception);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Conversation)) != 0) {
        logChannels.insert(LogChannel::Conversation);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Combat)) != 0) {
        logChannels.insert(LogChannel::Combat);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Script)) != 0) {
        logChannels.insert(LogChannel::Script);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Script2)) != 0) {
        logChannels.insert(LogChannel::Script2);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Script3)) != 0) {
        logChannels.insert(LogChannel::Script3);
    }
    if ((logChannelsMask & static_cast<int>(LogChannel::Render)) != 0 || vars["renderdiag"].as<bool>()) {
        logChannels.insert(LogChannel::Render);
    }
    options->logging.channels = std::move(logChannels);

    options->commandsFile = vars["commands-file"].as<std::string>();
    options->controlPort = vars["control-port"].as<int>();
    options->game.tickRate = vars["tickrate"].as<int>();
    if (options->game.tickRate < 1 || options->game.tickRate > 1000) {
        throw std::invalid_argument("tickrate must be between 1 and 1000");
    }
    options->game.regenRate = vars["regen-rate"].as<float>();
    if (!(options->game.regenRate >= 0.0f && options->game.regenRate <= 100.0f)) {
        throw std::invalid_argument("regen-rate must be between 0 and 100");
    }
    options->game.tickGate = vars["tick-gate"].as<bool>();
    options->game.interpolate = vars["interpolate"].as<bool>();
    options->game.immortalPlayer = vars["immortal-player"].as<bool>();
    options->game.pauseInMenus = vars["pause-in-menus"].as<bool>();
    options->game.friendlyCollisionOff = vars["friendly-collision-off"].as<bool>();
    options->headlessFrameTime = parseFrameTime(vars["headless-frametime"].as<std::string>());
    options->runUnfocused = vars["run-unfocused"].as<bool>();
    if (options->controlPort < 0 || options->controlPort > 65535) {
        throw std::invalid_argument("control-port must be between 0 and 65535");
    }

    options->coop.hostPort = vars["host"].as<int>();
    options->coop.joinAddress = vars["join"].as<std::string>();
    options->coop.mode = vars["coop-mode"].as<std::string>();
    options->coop.name = vars["coop-name"].as<std::string>();
    options->coop.slot = vars["coop-slot"].as<std::string>();
    options->coop.hostOnlyTransitions = vars["coop-host-only-transitions"].as<bool>();
    options->coop.hostOnlyTrading = vars["coop-host-only-trading"].as<bool>();
    options->coop.noPause = vars["coop-no-pause"].as<bool>();
    options->coop.pauseCooldown = vars["coop-pause-cooldown"].as<float>();
    options->coop.enemyScaling = vars["coop-enemy-scaling"].as<float>();
    options->coop.server = vars["coop-server"].as<std::string>();
    options->coop.relayOnly = vars["coop-relay-only"].as<bool>();
    options->coop.askJoin = vars["coop-ask-join"].as<int>();
    options->coop.simLagMs = vars["coop-sim-lag"].as<int>();
    options->coop.simLossPercent = vars["coop-sim-loss"].as<float>();
    options->coop.simJitterMs = vars["coop-sim-jitter"].as<int>();
    options->coop.recordPath = vars["coop-record"].as<std::string>();
    if (options->coop.askJoin < -1 || options->coop.askJoin > 1) {
        throw std::invalid_argument("coop-ask-join must be 0 or 1");
    }
    if (options->coop.simLagMs < 0 || options->coop.simLagMs > 5000 || options->coop.simJitterMs < 0 || options->coop.simJitterMs > 2000 ||
        !(options->coop.simLossPercent >= 0.0f && options->coop.simLossPercent <= 90.0f)) {
        throw std::invalid_argument("coop-sim-lag must be 0-5000 ms, coop-sim-jitter 0-2000 ms and coop-sim-loss 0-90 percent");
    }
    if (!options->coop.server.empty()) {
        std::string host;
        uint16_t port = 0;
        if (!net::parseServiceAddress(options->coop.server, host, port)) {
            throw std::invalid_argument("coop-server must be host or host:port");
        }
    }
    if (!(options->coop.pauseCooldown >= 0.0f && options->coop.pauseCooldown <= 3600.0f)) {
        throw std::invalid_argument("coop-pause-cooldown must be between 0 and 3600 seconds");
    }
    if (!(options->coop.enemyScaling >= 0.0f && options->coop.enemyScaling <= 4.0f)) {
        throw std::invalid_argument("coop-enemy-scaling must be between 0 and 4");
    }
    if (options->coop.hostPort < 0 || options->coop.hostPort > 65535) {
        throw std::invalid_argument("host must be a UDP port between 1 and 65535");
    }
    if (options->coop.hostPort > 0 && !vars["join"].as<std::string>().empty()) {
        throw std::invalid_argument("--host and --join are exclusive");
    }
    // "--join 1.2.3.4" or "--join some.host" has no port: use the default one.
    options->coop.joinAddress = net::withDefaultPort(options->coop.joinAddress, net::kDefaultCoopPort);
    if (!options->coop.joinAddress.empty() && !net::looksLikeAddress(options->coop.joinAddress)) {
        // Not ip:port, so a join code: XXXX-XXXX, any case, dash optional.
        auto code = net::normalizeJoinCode(options->coop.joinAddress);
        if (code.empty()) {
            throw std::invalid_argument("join must be a join code (8 letters and digits, e.g. K7QM-4TXR) or ip:port");
        }
        if (options->coop.server.empty()) {
            throw std::invalid_argument("joining by code needs --coop-server <host[:port]> (or coop-server= in reone.cfg)");
        }
        options->coop.joinCode = code;
        options->coop.joinAddress.clear();
    }
    if (options->coop.mode != "story" && options->coop.mode != "free") {
        throw std::invalid_argument("coop-mode must be story or free");
    }
    options->coop.password = vars["coop-password"].as<std::string>();

    if (vars["dedicated"].as<bool>()) {
        auto &dedicated = options->dedicated;
        dedicated.enabled = true;
        if (!options->coop.joinAddress.empty() || !options->coop.joinCode.empty()) {
            throw std::invalid_argument("--dedicated and --join are exclusive");
        }
        dedicated.port = options->coop.hostPort > 0 ? static_cast<uint16_t>(options->coop.hostPort) : game::kDedicatedDefaultPort;
        dedicated.maxPlayers = vars["coop-max-players"].as<int>();
        dedicated.password = options->coop.password;
        dedicated.server = options->coop.server;
        int slot = vars["dedicated-save-slot"].as<int>();
        dedicated.saveSlot = slot < 0 ? 0xFFFFFFFFu : static_cast<uint32_t>(slot);
        dedicated.autosaveSeconds = vars["dedicated-autosave"].as<float>();
        dedicated.startModule = vars["dedicated-start-module"].as<std::string>();
        dedicated.adminStdin = vars["dedicated-stdin"].as<bool>();
        if (auto problem = game::validateDedicatedSettings(dedicated); !problem.empty()) {
            throw std::invalid_argument(problem);
        }
        // No window, no sound, and a Free Mode session (the host starts it itself).
        options->graphics.headless = true;
        options->audio.muted = true;
        options->coop.mode = "free";
        options->coop.hostPort = 0;
    }

    return options;
}

} // namespace reone
