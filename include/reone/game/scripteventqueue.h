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

#pragma once

#include <deque>
#include <memory>

#include "event.h"

namespace reone {

namespace game {

class Game;
class Object;
struct SavedScriptEvent;

// Script event types as a saved game stores them.
constexpr uint16_t kSavedScriptEventSpellCastAt = 2;
// Ours: the engine's own numbers for events saves have no known type for.
constexpr uint16_t kSavedScriptEventConversation = 0x7001;
constexpr uint16_t kSavedScriptEventActivateItem = 0x7002;
constexpr uint16_t kSavedScriptEventUserDefined = 11;

/** The saved form of a script event: its type, then its parameters. */
SavedScriptEvent savedScriptEventOf(const Event &event);

/**
 * A script event from its saved form. Types other than the two above are
 * read as user-defined events numbered by the type, which is how earlier
 * reone saves stored every event.
 */
std::shared_ptr<Event> eventFromSaved(
    const SavedScriptEvent &saved,
    std::vector<std::shared_ptr<Object>> objects);

/**
 * Script events (SignalEvent) wait in one queue and run on a later drain in
 * the simulation tick, never inside the script that signalled them.
 * K2 behaviour: the queue is ordered by due time, first in first out among
 * equal times. A drain runs every event that is due, including events
 * signalled by the scripts it runs. An event whose target no longer exists is
 * dropped. See design note routines_batch1, "EventSpellCastAt / SignalEvent".
 */
class ScriptEventQueue {
public:
    struct Entry {
        uint64_t dueMilliseconds {0};
        uint32_t target {0};
        uint32_t caller {0};
        std::shared_ptr<Event> event;
    };

    // A drain stops after this many events and leaves the rest for the next
    // drain, so a script that keeps signalling itself cannot hang the tick.
    static constexpr size_t kMaxEventsPerDrain = 4096;

    explicit ScriptEventQueue(Game &game) :
        _game(game) {
    }

    /** Queue an event due at the current world time. */
    void signal(uint32_t target, uint32_t caller, std::shared_ptr<Event> event);

    /** Queue an event due at an absolute world time (restored saves). */
    void enqueueAt(uint64_t dueMilliseconds, uint32_t target, uint32_t caller, std::shared_ptr<Event> event);

    /** Run every due event. Returns the number of events run. */
    size_t runDue();

    void clear() { _entries.clear(); }

    const std::deque<Entry> &entries() const { return _entries; }

    /** Run one event on its target now. Used by the drain. */
    static void dispatch(Game &game, Object &target, uint32_t caller, const Event &event);

private:
    Game &_game;
    std::deque<Entry> _entries;
    bool _draining {false};
};

} // namespace game

} // namespace reone
