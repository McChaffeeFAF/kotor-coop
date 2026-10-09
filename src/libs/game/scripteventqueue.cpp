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

#include "reone/game/scripteventqueue.h"

#include <algorithm>

#include "reone/game/coopsession.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/savedruntime.h"
#include "reone/game/script/runner.h"
#include "reone/script/types.h"
#include "reone/script/variable.h"
#include "reone/system/logutil.h"

using namespace reone::script;

namespace reone {

namespace game {

SavedScriptEvent savedScriptEventOf(const Event &event) {
    SavedScriptEvent saved;
    if (event.kind() == EventKind::SpellCastAt) {
        saved.type = kSavedScriptEventSpellCastAt;
        saved.integers = event.integers();
    } else if (event.kind() == EventKind::Conversation) {
        saved.type = kSavedScriptEventConversation;
    } else if (event.kind() == EventKind::ActivateItem) {
        saved.type = kSavedScriptEventActivateItem;
    } else {
        saved.type = kSavedScriptEventUserDefined;
        saved.integers = {static_cast<int32_t>(event.number())};
    }
    saved.floats = event.floats();
    saved.strings = event.strings();
    for (auto id : event.objects()) {
        saved.objects.emplace_back(id);
    }
    return saved;
}

std::shared_ptr<Event> eventFromSaved(
    const SavedScriptEvent &saved,
    std::vector<std::shared_ptr<Object>> objects) {
    if (saved.type == kSavedScriptEventSpellCastAt) {
        auto event = std::make_shared<Event>(0, saved.integers, saved.floats, saved.strings, std::move(objects));
        event->setKind(EventKind::SpellCastAt);
        return event;
    }
    if (saved.type == kSavedScriptEventConversation || saved.type == kSavedScriptEventActivateItem) {
        auto event = std::make_shared<Event>(0, saved.integers, saved.floats, saved.strings, std::move(objects));
        event->setKind(saved.type == kSavedScriptEventConversation ? EventKind::Conversation : EventKind::ActivateItem);
        return event;
    }
    if (saved.type == kSavedScriptEventUserDefined && !saved.integers.empty()) {
        return std::make_shared<Event>(
            saved.integers.front(),
            std::vector<int32_t> {},
            saved.floats,
            saved.strings,
            std::move(objects));
    }
    return std::make_shared<Event>(saved.type, saved.integers, saved.floats, saved.strings, std::move(objects));
}

void ScriptEventQueue::signal(uint32_t target, uint32_t caller, std::shared_ptr<Event> event) {
    enqueueAt(_game.worldTimeMilliseconds(), target, caller, std::move(event));
}

void ScriptEventQueue::enqueueAt(uint64_t dueMilliseconds, uint32_t target, uint32_t caller, std::shared_ptr<Event> event) {
    if (!event) {
        return;
    }
    Entry entry;
    entry.dueMilliseconds = dueMilliseconds;
    entry.target = target;
    entry.caller = caller;
    entry.event = std::move(event);
    // Insert before the first strictly later event: first in, first out among
    // events due at the same time.
    auto position = std::find_if(_entries.begin(), _entries.end(), [&](const Entry &other) {
        return other.dueMilliseconds > dueMilliseconds;
    });
    _entries.insert(position, std::move(entry));
}

size_t ScriptEventQueue::runDue() {
    if (_draining) {
        // The outer drain picks up anything queued meanwhile.
        return 0;
    }
    _draining = true;
    size_t count = 0;
    try {
        while (!_entries.empty() && count < kMaxEventsPerDrain) {
            if (_entries.front().dueMilliseconds > _game.worldTimeMilliseconds()) {
                break;
            }
            Entry entry = std::move(_entries.front());
            _entries.pop_front();
            ++count;
            auto target = _game.getObjectById(entry.target);
            if (!target) {
                continue;
            }
            dispatch(_game, *target, entry.caller, *entry.event);
        }
    } catch (...) {
        _draining = false;
        throw;
    }
    _draining = false;
    if (count == kMaxEventsPerDrain && !_entries.empty()) {
        warn("Script events: drain limit reached, " + std::to_string(_entries.size()) + " left for the next drain");
    }
    return count;
}

// EventSpellCastAt: run the target's OnSpellCastAt script, which reads the
// caster, spell and harmfulness back through GetLastSpellCaster and friends.
static void dispatchSpellCastAt(Game &game, Object &target, const Event &event) {
    std::string script;
    if (auto creature = dyn_cast<Creature>(&target)) {
        auto *coop = game.coop();
        if (coop && coop->isPlayerControlled(creature->id())) {
            return; // no AI reaction for a co-op player's companion
        }
        script = creature->getOnSpellAt();
    } else if (auto door = dyn_cast<Door>(&target)) {
        script = door->getOnSpellCastAt();
    } else if (auto placeable = dyn_cast<Placeable>(&target)) {
        script = placeable->getOnSpellCastAt();
    }
    if (script.empty()) {
        return;
    }
    auto objects = event.objects();
    uint32_t caster = objects.empty() ? kObjectInvalid : objects.front();
    if (caster == kObjectSelf) {
        caster = kObjectInvalid;
    }
    const auto &integers = event.integers();
    int spell = integers.size() > 0 ? integers[0] : -1;
    int harmful = integers.size() > 1 ? integers[1] : 0;
    game.scriptRunner().run(
        script,
        {{ArgKind::Caller, Variable::ofObject(target.id())},
         {ArgKind::LastSpellCaster, Variable::ofObject(caster)},
         {ArgKind::LastSpell, Variable::ofInt(spell)},
         {ArgKind::LastSpellHarmful, Variable::ofInt(harmful)}});
}

// EventConversation: run the target's OnDialogue script.
static void dispatchConversation(Game &game, Object &target) {
    auto creature = dyn_cast<Creature>(&target);
    if (!creature || creature->getOnDialogue().empty()) {
        return;
    }
    game.scriptRunner().run(creature->getOnDialogue(), target.id());
}

// EventActivateItem: run the module's OnActivateItem script as if the target
// (the activator) had used the item on the event's target, itself when none.
static void dispatchActivateItem(Game &game, Object &activator, const Event &event) {
    auto module = game.module();
    const auto &objects = event.objects();
    auto item = objects.empty() ? nullptr : game.getObjectById(objects[0]);
    if (!module || !item) {
        return;
    }
    auto target = objects.size() > 1 ? game.getObjectById(objects[1]) : nullptr;
    module->runOnActivateItem(*item, activator, target ? *target : activator);
}

void ScriptEventQueue::dispatch(Game &game, Object &target, uint32_t caller, const Event &event) {
    debug("Event run: " + target.tag() + " " + std::to_string(event.number()), LogChannel::Script);
    if (event.kind() == EventKind::Conversation) {
        dispatchConversation(game, target);
        return;
    }
    if (event.kind() == EventKind::ActivateItem) {
        dispatchActivateItem(game, target, event);
        return;
    }
    if (event.kind() == EventKind::SpellCastAt) {
        dispatchSpellCastAt(game, target, event);
        return;
    }
    game.scriptRunner().run(
        target.getOnUserDefined(),
        {{ArgKind::Caller, Variable::ofObject(target.id())},
         {ArgKind::UserDefinedEventNumber, Variable::ofInt(event.number())}});
}

} // namespace game

} // namespace reone
