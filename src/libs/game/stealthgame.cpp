/* Copyright (c) 2026 The reone project contributors */

// Stealth Mode, the game's side: the request path the key, the HUD toggle,
// scripts, the harness and co-op guests share, the feedback lines and the
// ways the mode ends. The rules themselves are in stealth.cpp; the
// perception checks are in Area (design note stealth).

#include "reone/game/game.h"

#include "reone/game/coopsession.h"
#include "reone/game/d20/k2feats.h"
#include "reone/game/di/services.h"
#include "reone/game/gui/confirmpopup.h"
#include "reone/game/messagelog.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/stealth.h"
#include "reone/resource/di/services.h"
#include "reone/resource/strings.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

/** The host's (or single player's) own hand: the leader, not a co-op guest's character. */
static bool isLocalHand(Game &game, const Creature &creature) {
    auto leader = game.party().getLeader();
    return leader.get() == &creature;
}

/** The leader has AI party members who would follow it (they hold still in solo mode). */
static bool leadsFollowers(Game &game, const Creature &creature) {
    if (!isLocalHand(game, creature)) {
        return false;
    }
    auto *coop = game.coop();
    for (const auto &member : game.party().members()) {
        auto &other = member.creature;
        if (!other || other.get() == &creature || other->isDead()) {
            continue;
        }
        if (coop && coop->isPlayerControlled(other->id())) {
            continue; // a co-op player's character goes its own way
        }
        return true;
    }
    return false;
}

stealth::Refusal Game::stealthRefusal(const Creature &creature) const {
    auto &self = const_cast<Game &>(*this);
    stealth::EntryInputs inputs;
    inputs.alive = !creature.isDead() && !creature.isTemporarilyDead();
    inputs.inCombat = creature.isInCombat();
    inputs.stealthUnitEquipped = creature.hasStealthUnitEquipped();
    inputs.stealthRanks = creature.attributes().getSkillRank(SkillType::Stealth);
    inputs.leaderWithFollowers = leadsFollowers(self, creature);
    inputs.soloMode = _party.isSoloMode();
    return stealth::entryRefusal(inputs);
}

void Game::stealthFeedback(const Creature &creature, bool on) {
    int strRef = stealth::kStrRefDeactivated;
    if (on) {
        bool run = k2feats::hasStealthRun([&creature](FeatType feat) { return creature.hasEffectiveFeat(feat); });
        strRef = run ? stealth::kStrRefActive : stealth::kStrRefActivated;
    }
    std::string text = _services.resource.strings.getText(strRef);
    if (text.empty()) {
        return;
    }
    _messageLog.add(MessageLog::kFeedbackMessageType, MessageLog::Style::Normal, text);
    if (_hud) {
        _hud->barkBubble().setBarkText(text, 3.0f);
    }
}

bool Game::requestStealth(Creature &creature, bool on, StealthAsk ask) {
    if (creature.isStealthed() == on) {
        return true;
    }
    bool local = isLocalHand(*this, creature);
    if (!on) {
        creature.setStealthMode(false);
        if (local) {
            stealthFeedback(creature, false);
        }
        return true;
    }
    auto refusal = stealthRefusal(creature);
    if (refusal == stealth::Refusal::NeedSolo) {
        if (ask == StealthAsk::AssumeYes) {
            _party.setSoloMode(true);
            refusal = stealth::Refusal::None;
        } else if (ask == StealthAsk::NoPrompt) {
            refusal = stealth::Refusal::None;
        } else {
            // TLK 37890: "To use Stealth, you must turn Solo Mode on. Do you wish to turn Solo Mode on?"
            if (!_confirmPopup) {
                _confirmPopup = tryLoadGUI<ConfirmPopup>();
            }
            if (!_confirmPopup || _confirmPopup->isVisible()) {
                return false;
            }
            uint32_t id = creature.id();
            _confirmPopup->showConfirm(
                _services.resource.strings.getText(stealth::kStrRefNeedSolo),
                [this, id]() {
                    if (auto again = getObjectById<Creature>(id)) {
                        requestStealth(*again, true, StealthAsk::AssumeYes);
                    }
                });
            return false;
        }
    }
    if (refusal != stealth::Refusal::None) {
        int strRef = stealth::refusalStrRef(refusal);
        if (local && strRef >= 0) {
            std::string text = _services.resource.strings.getText(strRef);
            _messageLog.add(MessageLog::kFeedbackMessageType, MessageLog::Style::Normal, text);
            if (_hud) {
                _hud->barkBubble().setBarkText(text, 3.0f);
            }
        }
        return false;
    }
    creature.setStealthMode(true);
    info("Stealth: " + creature.tag() + " enters Stealth Mode");
    if (local) {
        stealthFeedback(creature, true);
    }
    return true;
}

void Game::endStealth(Creature &creature) {
    if (!creature.isStealthed()) {
        return;
    }
    creature.setStealthMode(false);
    info("Stealth: " + creature.tag() + " leaves Stealth Mode");
    if (isLocalHand(*this, creature)) {
        stealthFeedback(creature, false);
    }
}

void Game::toggleStealthAsKey() {
    auto leader = _party.getLeader();
    if (!leader) {
        return;
    }
    if (_coop && _coop->isClient()) {
        // Co-op: the host decides; the HUD follows the host's snapshot.
        _coop->submitRequest(net::CommandType::Stealth, 0, leader->isStealthed() ? 0 : 1);
        return;
    }
    requestStealth(*leader, !leader->isStealthed(), StealthAsk::Prompt);
}

void Game::endStealthForConversation() {
    // K2 (SetKeepStealthInDialog, 819): "By default, stealth is removed from
    // anyone taking part in a cutscene." The flag is the module's, unsaved.
    if (_module && _module->scriptSetting("KeepStealthInDialog") != 0) {
        return;
    }
    for (const auto &member : _party.members()) {
        if (member.creature && member.creature->isStealthed()) {
            endStealth(*member.creature);
        }
    }
}

} // namespace game

} // namespace reone
