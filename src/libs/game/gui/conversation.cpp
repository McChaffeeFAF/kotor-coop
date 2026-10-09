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

#include "reone/game/gui/conversation.h"

#include "reone/game/conversationleader.h"

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/di/services.h"
#include "reone/gui/control/listbox.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/lips.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/resources.h"
#include "reone/system/logutil.h"

#include "reone/game/di/services.h"
#include "reone/game/coopsession.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/script/runner.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr float kDefaultEntryDuration = 3.0f;

static bool g_allEntriesSkippable = false;

static script::ArgKind getScriptParamArgKind(size_t index) {
    switch (index) {
    case 0:
        return script::ArgKind::ScriptParam1;
    case 1:
        return script::ArgKind::ScriptParam2;
    case 2:
        return script::ArgKind::ScriptParam3;
    case 3:
        return script::ArgKind::ScriptParam4;
    case 4:
    default:
        return script::ArgKind::ScriptParam5;
    }
}

template <typename Params>
static std::vector<script::Argument> makeScriptArgs(uint32_t callerId, const Params &params) {
    std::vector<script::Argument> args;
    if (callerId) {
        args.emplace_back(script::ArgKind::Caller, script::Variable::ofObject(callerId));
    }
    for (size_t i = 0; i < params.ints.size(); ++i) {
        args.emplace_back(getScriptParamArgKind(i), script::Variable::ofInt(params.ints[i]));
    }
    args.emplace_back(script::ArgKind::ScriptStringParam, script::Variable::ofString(params.str));
    return args;
}

bool Conversation::isCurrent(uint64_t generation) const {
    return generation == _generation && _game.globalFade().isCurrentDialog(_fadeDialog);
}

void Conversation::start(const std::shared_ptr<Dialog> &dialog, const std::shared_ptr<Object> &owner,
                          GlobalFade::DialogTicket admission) {
    if (!admission) {
        admission = _game.globalFade().admitDialog(/*replace=*/true);
    }
    if (!_game.globalFade().isCurrentDialog(admission)) {
        return;
    }
    auto generation = ++_generation;
    _fadeDialog = std::move(admission);
    // Each conversation starts with its own fade layer clear.
    _game.dialogFade().resetSession();
    if (_dialog) {
        auto oldDialog = _dialog;
        auto oldOwner = _owner.resolve();
        bool wasRunning = !_ended;
        _ended = true;
        onFinish();
        if (!isCurrent(generation)) {
            return;
        }
        if (oldOwner) {
            oldOwner->setIsInConversation(false);
        }
        // Replacing a conversation that had not reached its end cuts it off.
        if (wasRunning && oldOwner && !oldDialog->abortScript.empty()) {
            _game.scriptRunner().run(oldDialog->abortScript, oldOwner->id());
            if (!isCurrent(generation)) {
                return;
            }
        }
    }
    debug("Start " + dialog->resRef, LogChannel::Conversation);

    _paused = false;
    _ended = false;
    _oneLinerEnding = false;
    _dialog = dialog;
    _owner = owner;

    if (owner) {
        owner->setIsInConversation(true);
    }

    if (_ambientTrack) {
        _ambientTrack->stop();
        _ambientTrack.reset();
    }
    if (!dialog->ambientTrack.empty()) {
        // K2 behaviour: plays beside the area music, not instead of it.
        _ambientTrack = _game.playSoundCue("dialogambient", dialog->ambientTrack, std::nullopt, AudioType::Music, /*loop=*/true);
    }
    loadConversationBackground();
    loadCameraModel();
    onStart();
    if (isCurrent(generation)) {
        loadStartEntry();
    }
}

static BackgroundType getBackgroundType(ComputerType compType) {
    switch (compType) {
    case ComputerType::Rakatan:
        return BackgroundType::Computer1;
    default:
        return BackgroundType::Computer0;
    }
}

void Conversation::loadConversationBackground() {
    if (_dialog->conversationType == ConversationType::Computer) {
        loadBackground(getBackgroundType(_dialog->computerType));
    } else {
        loadBackground(BackgroundType::None);
    }
}

void Conversation::loadCameraModel() {
    std::string modelResRef(_dialog->cameraModel);
    _cameraModel = modelResRef.empty() ? nullptr : _services.resource.models.get(modelResRef);
}

void Conversation::setBarkText(std::string text, float duration) {
    _game.setBarkBubbleText(std::move(text), duration);
}

void Conversation::onStart() {
}

void Conversation::loadStartEntry() {
    auto generation = _generation;
    auto dialog = _dialog;
    int entryIdx = indexOfFirstActive(dialog->startEntries);
    if (!isCurrent(generation)) {
        return;
    }
    if (entryIdx == -1) {
        // Nothing was ever shown, so the conversation never stood at an end
        // node: this is an abort, not a normal end.
        debug("Abort (no active start entry)", LogChannel::Conversation);
        end(/*normal=*/false);
        return;
    }
    loadEntry(entryIdx, true);
}

int Conversation::indexOfFirstActive(const std::vector<Dialog::EntryReplyLink> &links) {
    auto generation = _generation;
    for (auto &link : links) {
        bool active = isLinkActive(link);
        if (!isCurrent(generation)) {
            return -1;
        }
        if (active) {
            return link.index;
        }
    }
    return -1;
}

bool Conversation::isLinkActive(const Dialog::EntryReplyLink &link) {
    auto generation = _generation;
    std::optional<bool> active;
    if (!link.active.empty()) {
        active = evaluateCondition(link.active, link.params);
        if (!isCurrent(generation)) {
            return false;
        }
        if (link.notActive) {
            active = !active.value();
        }
    }
    std::optional<bool> active2;
    if (!link.active2.empty()) {
        active2 = evaluateCondition(link.active2, link.params2);
        if (!isCurrent(generation)) {
            return false;
        }
        if (link.notActive2) {
            active2 = !active2.value();
        }
    }
    if (!active && !active2) {
        return true;
    }
    if (!active) {
        return active2.value();
    }
    if (!active2) {
        return active.value();
    }
    return link.logic == 1 ? active.value() || active2.value() : active.value() && active2.value();
}

bool Conversation::evaluateCondition(const std::string &scriptResRef, const Dialog::EntryReplyLink::ConditionParams &params) {
    auto owner = _owner.resolve();
    return _game.scriptRunner().run(
               scriptResRef,
               makeScriptArgs(owner ? owner->id() : 0, params)) != 0;
}

void Conversation::runScript(const std::string &scriptResRef, const Dialog::EntryReply::ActionParams &params) {
    if (!scriptResRef.empty()) {
        auto owner = _owner.resolve();
        _game.scriptRunner().run(
            scriptResRef,
            makeScriptArgs(owner ? owner->id() : 0, params));
    }
}

void Conversation::runScripts(const Dialog::EntryReply &node) {
    auto generation = _generation;
    runScript(node.script, node.actionParams);
    if (isCurrent(generation)) {
        runScript(node.script2, node.actionParams2);
    }
}

void Conversation::applyNodeFade(const Dialog::EntryReply &node) {
    if (node.fadeType == 0) {
        return;
    }
    // Types 1 and 4 fade out to the colour; every other type fades in from it.
    bool out = node.fadeType == 1 || node.fadeType == 4;
    _game.dialogFade().request(
        out ? GlobalFade::Direction::Out : GlobalFade::Direction::In,
        node.fadeDelay,
        node.fadeLength,
        node.fadeColor);
}

void Conversation::applyStatusSummaryEntries(const Dialog::EntryReply &node) {
    if (!node.quest.empty()) {
        _game.journal().addEntry(node.quest, static_cast<int>(node.questEntry));
    }
    _game.awardPlotXPByIndex(node.plotIndex, node.plotXPPercentage);
}

void Conversation::finish() {
    end(/*normal=*/true);
}

void Conversation::abort() {
    if (!isRunning()) {
        return;
    }
    debug("Abort", LogChannel::Conversation);
    end(/*normal=*/false);
}

void Conversation::end(bool normal) {
    auto generation = ++_generation;
    auto dialog = _dialog;
    auto ownerRef = _owner;
    bool wasRunning = !_ended;
    bool oneLiner = _oneLinerEnding;
    _ended = true;
    _oneLinerEnding = false;
    if (wasRunning && dialog) {
        EndRecord record;
        record.dialog = dialog->resRef;
        if (_currentEntry && !dialog->entries.empty() && _currentEntry >= dialog->entries.data() &&
            _currentEntry < dialog->entries.data() + dialog->entries.size()) {
            record.entry = static_cast<int>(_currentEntry - dialog->entries.data());
        }
        record.normal = normal;
        record.tick = _game.tickCount();
        static constexpr size_t kMaxEndHistory = 256;
        if (_endHistory.size() >= kMaxEndHistory) {
            _endHistory.erase(_endHistory.begin());
        }
        _endHistory.push_back(std::move(record));
    }
    _game.globalFade().finishDialog(_fadeDialog);
    _fadeDialog.reset();
    _game.dialogFade().resetSession();
    _game.disableVideoEffect();
    if (_ambientTrack) {
        _game.fadeOutSoundCue(std::move(_ambientTrack), 0.5f);
        _ambientTrack.reset();
    }
    _paused = false;
    _entryEnded = true;
    _placeableCamera.reset();
    _game.dialogLocks().clear();
    onFinish();
    if (_generation != generation) {
        return;
    }

    // A reply script can hand the screen to something else before the
    // conversation ends -- PlayPazaak opens the pazaak board from a dialogue
    // action -- so only return to the world if the conversation still owns it.
    if (_game.currentScreen() == Game::Screen::Conversation) {
        _game.openInGame();
    }

    // A normal end runs EndConversation, an abort EndConverAbort; either
    // needs the owner as the script's caller.
    auto owner = ownerRef.resolve();
    if (wasRunning && owner && dialog) {
        const std::string &script = normal ? dialog->endScript : dialog->abortScript;
        if (!script.empty()) {
            _game.scriptRunner().run(script, owner->id());
        }
    }

    // Then every creature in the area hears that the dialogue is over (only
    // the owner for a one-liner). The events are queued on the creatures and
    // run in their next simulation update.
    if (wasRunning && normal) {
        if (oneLiner) {
            auto oneLinerOwner = ownerRef.resolve();
            if (auto creature = oneLinerOwner ? dyn_cast<Creature>(oneLinerOwner) : nullptr) {
                creature->signalEndDialogue();
            } else if (auto placeable = oneLinerOwner ? dyn_cast<Placeable>(oneLinerOwner) : nullptr) {
                placeable->signalEndDialogue();
            }
        } else {
            signalEndDialogue(ownerRef.resolve());
        }
    }

    if (_generation == generation) {
        if (auto owner = ownerRef.resolve()) {
            owner->setIsInConversation(false);
        }
        switchBackAfterConversation(_game);
    }
}

void Conversation::onFinish() {
}

void Conversation::signalEndDialogue(const std::shared_ptr<Object> &owner) {
    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) {
        if (auto creature = owner ? dyn_cast<Creature>(owner) : nullptr) {
            creature->signalEndDialogue();
        } else if (auto placeable = owner ? dyn_cast<Placeable>(owner) : nullptr) {
            placeable->signalEndDialogue();
        }
        return;
    }
    for (const auto &object : area->getObjectsByType(ObjectType::Creature)) {
        if (auto creature = dyn_cast<Creature>(object)) {
            creature->signalEndDialogue();
        }
    }
    for (const auto &object : area->getObjectsByType(ObjectType::Placeable)) {
        if (auto placeable = dyn_cast<Placeable>(object)) {
            placeable->signalEndDialogue();
        }
    }
}

void Conversation::cleanupForModuleTransition() {
    auto generation = ++_generation;
    // The area is being unloaded: neither end script runs.
    _ended = true;
    _oneLinerEnding = false;
    _game.globalFade().finishDialog(_fadeDialog);
    _fadeDialog.reset();
    _game.dialogFade().resetSession();
    _game.disableVideoEffect();
    if (_ambientTrack) {
        _game.fadeOutSoundCue(std::move(_ambientTrack), 0.5f);
        _ambientTrack.reset();
    }
    _paused = false;
    _entryEnded = true;
    _placeableCamera.reset();
    _game.dialogLocks().clear();
    if (!_dialog) {
        return;
    }
    if (_currentVoice) {
        _currentVoice->stop();
        _currentVoice.reset();
    }
    _lipAnimation.reset();
    onFinish();
    if (_generation == generation) {
        if (auto owner = _owner.resolve()) {
            owner->setIsInConversation(false);
        }
    }
}

void Conversation::loadEntry(int index, bool start) {
    auto generation = _generation;
    auto dialog = _dialog; // retain nodes across callbacks, including conditions
    debug("Load entry " + std::to_string(index), LogChannel::Conversation);
    _currentEntry = &_dialog->getEntry(index);
    // A placeable camera cut lasts for the entry whose script asked for it.
    _placeableCamera.reset();

    applyStatusSummaryEntries(*_currentEntry);

    std::string entryText(_game.substituteCustomTokens(_currentEntry->text));
    setMessage(entryText);
    loadReplies();
    if (!isCurrent(generation)) {
        return;
    }
    loadVoiceOver();

    // Entry publication consumes only the current handoff. An entry action
    // below can immediately replace this automatic reveal with its own fade.
    _game.globalFade().revealDialog(_fadeDialog);
    applyNodeFade(*_currentEntry);
    // A node framed by the dialog camera applies its video effect; an animated
    // camera keeps whatever effect is showing.
    if (!usesCameraAnimation(_currentEntry)) {
        _game.applyDialogVideoEffect(_currentEntry->camVidEffect);
    }

    // Run entry scripts. An entry action can start another conversation, which
    // replaces this one outright. Holding the dialogue keeps this entry and its
    // replies alive for the script to act on, and tells us to stop rather than
    // carry on driving the new session with the old one's state.
    runScripts(*_currentEntry);
    if (!isCurrent(generation)) {
        return;
    }

    // Conversation is a one-liner if there is exactly one empty reply that has no entries
    bool oneLiner = false;
    if (start && _replies.size() == 1ll) {
        const Dialog::EntryReply &reply = *_replies[0];
        oneLiner = reply.text.empty() && reply.entries.empty();
    }
    if (!oneLiner && isNonPresentationalEntry()) {
        pickReply(0);
        return;
    }

    scheduleEndOfEntry();
    onLoadEntry();
    if (!isCurrent(generation)) {
        return;
    }

    if (oneLiner) {
        setBarkText(std::move(entryText), _entryDuration);
        debug("Dialog: finish (one-liner)");

        // Barking the entry instead of opening the conversation GUI is a
        // presentation choice, not a reason to drop the sole terminal reply's
        // action. Resolving that reply through pickReply keeps the usual
        // ordering and lets it terminate the conversation, so nothing here
        // finishes it a second time. Ending the entry first stops the update
        // timer from auto-picking the same reply again afterwards, and leaves
        // a replacement conversation's own entry state untouched.
        _entryEnded = true;
        _oneLinerEnding = true;
        pickReply(0);
        _oneLinerEnding = false;
        return;
    }

    if (_autoSkip) {
        if (std::optional<bool> skip = _autoSkip->trySkipEntry()) {
            if (skip.value() && !_paused) {
                endCurrentEntry();
            }
        }
    }
}

void Conversation::onLoadEntry() {
}

void Conversation::loadVoiceOver() {
    // Stop previous voice, if any
    if (_currentVoice) {
        _currentVoice->stop();
        _currentVoice.reset();
        _lipAnimation.reset();
    }

    // Play current voice over either from Sound or from VO_ResRef
    std::string voiceResRef;
    if (!_currentEntry->sound.empty()) {
        voiceResRef = _currentEntry->sound;
        _lipAnimation = _services.resource.lips.get(_currentEntry->sound);
    }
    if (!_currentEntry->voResRef.empty()) {
        if (voiceResRef.empty()) {
            voiceResRef = _currentEntry->voResRef;
        }
        if (!_lipAnimation) {
            _lipAnimation = _services.resource.lips.get(_currentEntry->voResRef);
        }
    }
    if (!voiceResRef.empty()) {
        auto clip = _services.resource.audioClips.get(voiceResRef);
        if (clip) {
            _currentVoice = _services.audio.mixer.play(std::move(clip), AudioType::Voice);
            _voiceElapsed = 0.0f;
        }
    }
}

static std::string getCameraAnimationName(int ordinal) {
    return str(boost::format("cut%03dw") % (ordinal - 1200 + 1));
}

void Conversation::scheduleEndOfEntry() {
    float duration = kDefaultEntryDuration;

    if (_cameraModel && usesCameraAnimation(_currentEntry) && (_currentEntry->waitFlags & Dialog::WaitFlags::waitAnimFinish)) {
        std::string animName(getCameraAnimationName(_currentEntry->cameraAnimation));
        std::shared_ptr<Animation> animation(_cameraModel->getAnimation(animName));
        if (animation) {
            duration = animation->length();
        }
    } else if (_currentEntry->delay != -1) {
        duration = static_cast<float>(_currentEntry->delay);
    } else if (_currentVoice) {
        duration = _currentVoice->duration();
    }

    _entryEnded = false;
    _entryDuration = duration;
    _endEntryTimer.reset(duration);
}

void Conversation::loadReplies() {
    auto generation = _generation;
    auto dialog = _dialog;
    _replies.clear();
    for (auto &link : _currentEntry->replies) {
        bool active = isLinkActive(link);
        if (!isCurrent(generation)) {
            return;
        }
        if (active) {
            _replies.push_back(&_dialog->getReply(link.index));
        }
    }

    // If there is only one empty reply, pick it automatically when the current entry ends
    _autoPickFirstReply = _replies.size() == 1ll && _replies.front()->text.empty();

    refreshReplies();
}

static std::string getReplyText(const Dialog::EntryReply &reply, int index, const Game &game) {
    return str(boost::format("%d. %s") % (index + 1) % (reply.text.empty() ? "[empty]" : game.substituteCustomTokens(reply.text)));
}

void Conversation::refreshReplies() {
    std::vector<std::string> lines;
    if (!_autoPickFirstReply) {
        for (size_t i = 0; i < _replies.size(); ++i) {
            lines.push_back(getReplyText(*_replies[i], static_cast<int>(i), _game));
        }
    }
    setReplyLines(std::move(lines));
}

bool Conversation::pickReplyAtRow(int row) {
    if (row < 0 || row >= static_cast<int>(_replies.size())) {
        return false;
    }
    pickReply(row);
    return true;
}

void Conversation::pickReply(int index) {
    if (_replica) {
        // A co-op client watches: the host picks, unless the host handed
        // this player the terminal (then the pick goes to the host).
        if (auto *coop = _game.coop()) {
            coop->clientPickReply(index);
        }
        return;
    }
    auto generation = _generation;
    debug("Pick reply " + std::to_string(index), LogChannel::Conversation);
    const Dialog::EntryReply &reply = *_replies[index];

    applyStatusSummaryEntries(reply);
    applyNodeFade(reply);

    // Run reply scripts
    auto dialog = _dialog;
    runScripts(reply);

    // A reply action can start another conversation, replacing this one. Going
    // on would advance or finish the new session in place of the old one.
    if (!isCurrent(generation)) {
        return;
    }

    int entryIdx = indexOfFirstActive(reply.entries);
    if (!isCurrent(generation)) {
        return;
    }
    if (entryIdx == -1) {
        debug("Finish (no active entries)", LogChannel::Conversation);
        finish();
        return;
    }
    loadEntry(entryIdx);
}

bool Conversation::handle(const input::Event &event) {
    switch (event.type) {
    case input::EventType::MouseButtonDown:
        if (handleMouseButtonDown(event.button))
            return true;
        break;
    case input::EventType::KeyUp:
        if (handleKeyUp(event.key))
            return true;
        break;
    default:
        break;
    }

    return GameGUI::handle(event);
}

bool Conversation::handleMouseButtonDown(const input::MouseButtonEvent &event) {
    if (_replica) {
        return event.button == input::MouseButton::Left;
    }
    if (event.button == input::MouseButton::Left && !_entryEnded && isSkippableEntry()) {
        _endEntryTimer.reset(0);
        return true;
    }
    return false;
}

bool Conversation::isSkippableEntry() const {
    return g_allEntriesSkippable || (_dialog->isSkippable() && !_paused);
}

bool Conversation::isNonPresentationalEntry() const {
    return _autoPickFirstReply &&
           _currentEntry->text.empty() &&
           _currentEntry->sound.empty() &&
           _currentEntry->voResRef.empty() &&
           _currentEntry->cameraAnimation == 0 &&
           _currentEntry->cameraId == 0 &&
           _currentEntry->cameraAngle == 0 &&
           _currentEntry->animations.empty() &&
           _currentEntry->delay == -1;
}

void Conversation::endCurrentEntry() {
    if (!_currentEntry || _entryEnded || _paused) {
        return;
    }
    auto generation = _generation;
    _entryEnded = true;

    // Stop voice over, if any
    if (_currentVoice) {
        _currentVoice->stop();
        _currentVoice.reset();
    }

    onEntryEnded();
    if (!isCurrent(generation)) {
        return;
    }

    if (_autoPickFirstReply) {
        pickReply(0);
    } else if (_replies.empty()) {
        debug("Finish (no active replies", LogChannel::Conversation);
        finish();
    } else if (_autoSkip) {
        if (std::optional<int> reply = _autoSkip->trySkipReply()) {
            pickReply(reply.value());
        }
    }
}

void Conversation::onEntryEnded() {
}

bool Conversation::handleKeyUp(const input::KeyEvent &event) {
    if (_replica) {
        return false;
    }
    if (!_entryEnded) {
        return false;
    }

    using IntKeyCode = std::underlying_type_t<input::KeyCode>;
    auto code = static_cast<IntKeyCode>(event.code);
    auto key1 = static_cast<IntKeyCode>(input::KeyCode::Key1);
    auto key9 = static_cast<IntKeyCode>(input::KeyCode::Key9);

    if (code < key1 || code > key9) {
        return false;
    }

    size_t index = code - key1;
    if (index < _replies.size()) {
        pickReply(index);
    } else {
        debug("Invalid reply index: " + std::to_string(index), LogChannel::Conversation);
    }
    return true;
}

void Conversation::update(float dt) {
    GameGUI::update(dt);
}

// Co-op replica

void Conversation::startReplica(const std::shared_ptr<Dialog> &dialog, const std::shared_ptr<Object> &owner) {
    if (_dialog && !_ended) {
        endReplica();
    }
    ++_generation;
    debug("Start replica " + dialog->resRef, LogChannel::Conversation);
    _replica = true;
    _paused = false;
    _ended = false;
    _oneLinerEnding = false;
    _dialog = dialog;
    _owner = owner;
    _currentEntry = nullptr;
    _replies.clear();
    _entryEnded = false;
    if (owner) {
        owner->setIsInConversation(true);
    }
    if (_ambientTrack) {
        _ambientTrack->stop();
        _ambientTrack.reset();
    }
    if (!dialog->ambientTrack.empty()) {
        _ambientTrack = _game.playSoundCue("dialogambient", dialog->ambientTrack, std::nullopt, AudioType::Music, /*loop=*/true);
    }
    loadConversationBackground();
    loadCameraModel();
    onStart();
}

int Conversation::currentEntryIndex() const {
    if (!_dialog || !_currentEntry || _dialog->entries.empty() || _currentEntry < _dialog->entries.data() ||
        _currentEntry >= _dialog->entries.data() + _dialog->entries.size()) {
        return -1;
    }
    return static_cast<int>(_currentEntry - _dialog->entries.data());
}

void Conversation::showReplicaEntry(int entry, const std::vector<int> &replies, bool entryEnded) {
    if (!_replica || !_dialog || entry < 0 || entry >= static_cast<int>(_dialog->entries.size())) {
        return;
    }
    bool newEntry = entry != currentEntryIndex();
    if (newEntry) {
        _currentEntry = &_dialog->getEntry(entry);
        _placeableCamera.reset();
        _entryEnded = false;
        setMessage(_game.substituteCustomTokens(_currentEntry->text));
    }
    _replies.clear();
    for (int index : replies) {
        if (index >= 0 && index < static_cast<int>(_dialog->replies.size())) {
            _replies.push_back(&_dialog->getReply(index));
        }
    }
    _autoPickFirstReply = _replies.size() == 1ll && _replies.front()->text.empty();
    refreshReplies();
    if (newEntry) {
        loadVoiceOver();
        if (!usesCameraAnimation(_currentEntry)) {
            _game.applyDialogVideoEffect(_currentEntry->camVidEffect);
        }
        onLoadEntry();
    }
    if (entryEnded && !_entryEnded) {
        _entryEnded = true;
        if (_currentVoice) {
            _currentVoice->stop();
            _currentVoice.reset();
        }
        onEntryEnded();
    }
}

void Conversation::endReplica() {
    if (!_replica) {
        return;
    }
    ++_generation;
    _ended = true;
    _entryEnded = true;
    _paused = false;
    _placeableCamera.reset();
    if (_currentVoice) {
        _currentVoice->stop();
        _currentVoice.reset();
    }
    _lipAnimation.reset();
    _game.disableVideoEffect();
    if (_ambientTrack) {
        _game.fadeOutSoundCue(std::move(_ambientTrack), 0.5f);
        _ambientTrack.reset();
    }
    if (_dialog) {
        onFinish();
    }
    if (auto owner = _owner.resolve()) {
        owner->setIsInConversation(false);
    }
    _replica = false;
    if (_game.currentScreen() == Game::Screen::Conversation) {
        _game.openInGame();
    }
}

void Conversation::updateSimulation(float dt) {
    if (_dialog && !_owner.empty() && !_owner.resolve()) {
        // The owner vanished mid-conversation: it is cut off.
        if (isRunning()) {
            abort();
        } else {
            finish();
        }
        return;
    }
    if (!_entryEnded) {
        _endEntryTimer.update(dt);
        if (_currentVoice) {
            _voiceElapsed += dt;
        }
        bool voiceDone = _currentVoice && _voiceElapsed >= _currentVoice->duration();
        if (!_paused && (_endEntryTimer.elapsed() || voiceDone)) {
            endCurrentEntry();
        }
    }
}

// Cut ordinals a camera animation can be named by.
static constexpr int kMinCameraAnimation = 1000;
static constexpr int kMaxCameraAnimation = 1727;
static constexpr int kStaticCameraAngle = 6;

bool Conversation::isReplyListShown() const {
    return _entryEnded && !_autoPickFirstReply && !_replies.empty();
}

const resource::Dialog::EntryReply *Conversation::cameraNode() const {
    if (isReplyListShown()) {
        return _replies.front();
    }
    return _currentEntry;
}

bool Conversation::usesCameraAnimation(const resource::Dialog::EntryReply *node) const {
    // Only entries play camera animations, and only in a conversation that
    // names a camera model; any other node is framed by the dialog camera.
    return _dialog && node && node == _currentEntry && !_dialog->cameraModel.empty() &&
           node->cameraAnimation >= kMinCameraAnimation && node->cameraAnimation <= kMaxCameraAnimation;
}

CameraType Conversation::getCamera(int &cameraId) const {
    if (_placeableCamera) {
        cameraId = *_placeableCamera;
        return CameraType::Static;
    }
    auto node = cameraNode();
    if (!_dialog || !node) {
        return CameraType::Dialog; // a co-op replica before its first entry
    }
    if (usesCameraAnimation(node)) {
        return CameraType::Animated;
    }
    if (node->cameraAngle == kStaticCameraAngle || node->cameraId > 0) {
        cameraId = node->cameraId;
        return CameraType::Static;
    }
    return CameraType::Dialog;
}

void Conversation::pause() {
    _paused = true;
}

void Conversation::resume() {
    _paused = false;
}

std::optional<int> Conversation::AutoSkip::trySkipReply() {
    if (!enabled || replies.empty()) {
        return std::optional<int>();
    }
    auto reply = replies.front();
    replies.pop();
    return reply;
}

std::optional<bool> Conversation::AutoSkip::trySkipEntry() {
    if (!enabled || entries.empty()) {
        return std::optional<bool>();
    }
    auto entry = entries.front();
    entries.pop();
    return entry;
}

} // namespace game

} // namespace reone
