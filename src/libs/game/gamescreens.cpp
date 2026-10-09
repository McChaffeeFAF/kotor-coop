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

// The gameplay screens scripts open (store, level-up, upgrade bench,
// credits) and the movie queue. Behaviour notes: design note gui_screens.
//
// A script asks for a screen from inside a tick; the request is honoured at
// the start of a later tick (processPendingScreens), once the world view is
// showing. Trades, accepted levels and assembled upgrades run from the
// screens' buttons and the harness commands: the input/command path between
// ticks.

#include "reone/game/game.h"

#include "reone/game/coopsession.h"

#include <boost/format.hpp>

#include "reone/audio/mixer.h"
#include "reone/game/d20/classes.h"
#include "reone/game/gui/chargen.h"
#include "reone/game/gui/sounds.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/store.h"
#include "reone/game/party.h"
#include "reone/resource/provider/movies.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

bool Game::pendingScreenMayOpen() const {
    return _screen == Screen::InGame && _module && !_movie && _nextModule.empty();
}

void Game::processPendingScreens() {
    if (!pendingScreenMayOpen()) {
        return;
    }
    if (_pendingStore) {
        PendingStore request = std::move(*_pendingStore);
        _pendingStore.reset();
        if (auto store = request.store.resolve()) {
            openStore(store, request.customer.resolve(), request.bonusMarkUp, request.bonusMarkDown);
            return;
        }
    }
    if (_pendingUpgrade) {
        auto item = _pendingUpgrade->resolve();
        bool named = !_pendingUpgrade->empty();
        _pendingUpgrade.reset();
        // A named item that is gone opens nothing; no item opens the bench.
        if (item || !named) {
            openUpgradeScreen(item);
            return;
        }
    }
    if (_pendingLevelUp) {
        _pendingLevelUp = false;
        auto player = _party.player();
        if (player && player->isLevelUpPending()) {
            openLevelUp(player);
        }
    }
}

// --- Level-up ---------------------------------------------------------------

bool Game::requestLevelUpScreen() {
    auto player = _party.player();
    if (!player || !player->isLevelUpPending()) {
        return false;
    }
    _pendingLevelUp = true;
    return true;
}

std::string Game::levelUpThroughScreen(const std::shared_ptr<Creature> &member, bool recommended, int prestigeClass) {
    if (!member || !member->isLevelUpPending()) {
        return "";
    }
    int before = member->attributes().getAggregateLevel();
    int beforeMaxHP = member->maxHitPoints();
    int beforeMaxFP = member->maxForcePoints();
    openLevelUp(member);
    if (!_charGen || _screen != Screen::CharacterGeneration) {
        return "";
    }
    CharacterGeneration &charGen = *_charGen;
    LevelUpMenu &menu = charGen.levelUpMenu();
    std::string steps;
    // The K2 prestige step (K2 systems merge), when the level-up opens on it.
    if (charGen.isPrestigeScreenOpen()) {
        auto &prestige = charGen.prestige();
        if (prestigeClass >= 0 && prestige.select(static_cast<ClassType>(prestigeClass)) && prestige.accept()) {
            steps += str(boost::format(" prestige=%d") % prestigeClass);
        } else {
            prestige.skip();
            steps += " prestige=declined";
        }
    }
    // Each step as a player takes it: open it, Recommended, Accept.
    if (menu.hasAttributes()) {
        charGen.openAbilities();
        charGen.abilities().applyRecommended();
        if (!charGen.abilities().accept()) {
            charGen.openSteps();
        }
        steps += " attributes";
    }
    charGen.openSkills();
    if (recommended) {
        charGen.skills().applyRecommended();
    }
    charGen.skills().accept();
    steps += " skills";
    if (menu.hasFeats()) {
        charGen.openFeats();
        charGen.feats().applyRecommended();
        if (charGen.feats().accept()) {
            steps += " feats";
        } else {
            charGen.openSteps();
        }
    }
    if (menu.hasPowers()) {
        charGen.openPowers();
        charGen.powers().applyRecommended();
        if (charGen.powers().accept()) {
            steps += " powers";
        } else {
            charGen.openSteps();
        }
    }
    charGen.finish();
    auto clazz = member->attributes().getEffectiveClass();
    return str(boost::format("levelup %s %d -> %d class=%d hp=%d->%d fp=%d->%d steps:%s%s") %
               member->tag() % before % member->attributes().getAggregateLevel() %
               static_cast<int>(clazz) % beforeMaxHP % member->maxHitPoints() %
               beforeMaxFP % member->maxForcePoints() % steps %
               (member->isLevelUpPending() ? " (another level pending)" : ""));
}

// --- Store ------------------------------------------------------------------

void Game::requestOpenStore(const std::shared_ptr<Store> &store, const std::shared_ptr<Creature> &customer,
                            int bonusMarkUp, int bonusMarkDown) {
    if (!store) {
        return;
    }
    PendingStore request;
    request.store = store;
    request.customer = customer;
    request.bonusMarkUp = bonusMarkUp;
    request.bonusMarkDown = bonusMarkDown;
    _pendingStore = std::move(request);
}

void Game::openStore(const std::shared_ptr<Store> &store, const std::shared_ptr<Creature> &customer,
                     int bonusMarkUp, int bonusMarkDown) {
    if (!store) {
        return;
    }
    if (!_store) {
        _store = tryLoadGUI<StoreGUI>();
    }
    if (!_store) {
        return;
    }
    auto buyer = customer ? customer : _party.getLeader();
    if (!store->onOpenStore().empty() && _scriptRunner) {
        _scriptRunner->run(store->onOpenStore(), store->id());
    }
    stopMovement();
    setRelativeMouseMode(false);
    setCursorType(resource::CursorType::Default);
    _storeSession.open(store, buyer, bonusMarkUp, bonusMarkDown);
    _store->open();
    changeScreen(Screen::Store);
}

void Game::closeStore() {
    if (_coop && _coop->isClient()) {
        _coop->clientStoreClosed();
    }
    _storeSession.close();
    if (_screen == Screen::Store) {
        openInGame();
    }
}

StoreSession::Result Game::storeBuy(const std::shared_ptr<Item> &item) {
    // The local player is the host (co-op item 4); a networked client's
    // request would name its own creature here.
    auto result = _storeSession.buy(item, _party.getLeader().get());
    if (result == StoreSession::Result::Ok) {
        _services.audio.mixer.play(_services.game.guiSounds.getOnClick(), audio::AudioType::Sound);
    }
    return result;
}

StoreSession::Result Game::storeSell(const std::shared_ptr<Item> &item) {
    auto result = _storeSession.sell(item, _party.getLeader().get());
    if (result == StoreSession::Result::Ok) {
        _services.audio.mixer.play(_services.game.guiSounds.getOnClick(), audio::AudioType::Sound);
    }
    return result;
}

// --- Upgrade bench ----------------------------------------------------------

void Game::requestUpgradeScreen(const std::shared_ptr<Item> &item) {
    _pendingUpgrade = RuntimeObjectRef<Item>(item);
}

void Game::openUpgradeScreen(const std::shared_ptr<Item> &item) {
    if (!_upgrade) {
        _upgrade = tryLoadGUI<UpgradeGUI>();
    }
    if (!_upgrade) {
        return;
    }
    stopMovement();
    setRelativeMouseMode(false);
    setCursorType(resource::CursorType::Default);
    _upgrade->open(item);
    changeScreen(Screen::Upgrade);
}

void Game::closeUpgradeScreen() {
    if (_upgrade) {
        _upgrade->bench().cancel();
    }
    if (_screen == Screen::Upgrade) {
        openInGame();
    }
}

bool Game::assembleUpgrades() {
    if (!_upgrade) {
        return false;
    }
    return _upgrade->bench().assemble();
}

// --- Credits ----------------------------------------------------------------

void Game::startCreditSequence(bool transparentBackground) {
    if (!_credits) {
        _credits = tryLoadGUI<CreditsGUI>();
    }
    if (!_credits) {
        return;
    }
    stopMovement();
    setRelativeMouseMode(false);
    _credits->start(transparentBackground);
    if (_screen != Screen::Credits) {
        _creditsOriginScreen = _screen;
    }
    changeScreen(Screen::Credits);
}

bool Game::isCreditSequenceInProgress() const {
    return _credits && _credits->isRunning();
}

void Game::endCreditSequence() {
    if (_credits) {
        _credits->finish();
    }
    if (_screen == Screen::Credits) {
        if (_creditsOriginScreen == Screen::InGame || _creditsOriginScreen == Screen::None) {
            openInGame();
        } else {
            changeScreen(_creditsOriginScreen);
        }
    }
    _creditsOriginScreen = Screen::None;
}

// --- Movie queue ------------------------------------------------------------

void Game::queueMovie(const std::string &name, bool skippable) {
    _scriptMovieQueue.push_back({name, skippable});
}

void Game::playMovieQueue(bool allowSeparateSkips) {
    for (auto &movie : _scriptMovieQueue) {
        _playingMovieQueue.push_back(std::move(movie));
    }
    _scriptMovieQueue.clear();
    _movieQueueSeparateSkips = allowSeparateSkips;
    if (!_movie) {
        playNextQueuedMovie();
    }
}

bool Game::playNextQueuedMovie() {
    while (!_playingMovieQueue.empty()) {
        QueuedMovie next = std::move(_playingMovieQueue.front());
        _playingMovieQueue.pop_front();
        if (startVideo(next.name)) {
            _movieSkippable = next.skippable;
            return true;
        }
    }
    _movieSkippable = true;
    return false;
}

void Game::skipMovie() {
    if (!_movie) {
        return;
    }
    if (_coop && _coop->isClient()) {
        // Design item 7: only the host can skip.
        return;
    }
    if (!_movieSkippable) {
        return;
    }
    if (!_movieQueueSeparateSkips) {
        // Without separate skips, a skip drops the rest of the queue too.
        _playingMovieQueue.clear();
    }
    _movie->finish();
}

} // namespace game

} // namespace reone
