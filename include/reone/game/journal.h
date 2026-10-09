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

#include <map>
#include <string>
#include <vector>

#include "reone/resource/parser/gff/jrl.h"

namespace reone {

namespace resource {

class IGffs;
class IStrings;

} // namespace resource

namespace game {

/**
 * Tracks quest journal state. Static quest definitions (categories and their
 * entries) come from the global journal resource, while dynamic state is a
 * list of quests keyed by plot ID, each holding the ID of its current entry.
 */
class Journal {
public:
    struct EntryChange {
        int plotIndex {-1};
        float xpPercentage {0.0f};
    };

    struct Quest {
        std::string plotId;
        int state {0};
        uint32_t date {0};
        uint32_t time {0};
    };

    /**
     * A world-notes entry (AddJournalWorldEntry): free text under an index,
     * apart from the quests. It is kept for scripts and tests; the journal
     * screens do not list it and saves do not carry it.
     */
    struct WorldEntry {
        int index {0};
        std::string title;
        std::string text;
        int strref {-1};      // set by AddJournalWorldEntryStrref
        int titleStrref {-1};
    };

    Journal(resource::IGffs &gffs, resource::IStrings &strings) :
        _gffs(gffs),
        _strings(strings) {
    }

    /** Clear quest state and force a reload of the global journal. */
    void reset();

    /**
     * Add a quest, or advance an existing one to a higher state. A lower or
     * equal state is only applied when allowOverrideHigher is true.
     *
     * @param state ID of a journal entry within the quest category
     * @return true if quest state changed
     */
    bool addEntry(const std::string &plotId, int state, bool allowOverrideHigher = false);

    /** @return true if a quest with the given plot ID was removed */
    bool removeEntry(const std::string &plotId);

    /** Set quest state verbatim, e.g. when restoring from a savegame. */
    void restoreEntry(std::string plotId, int state, uint32_t date, uint32_t time);

    /** @return state of the quest with the given plot ID, or 0 if not present */
    int getEntryState(const std::string &plotId) const;

    /** @return true if the current entry of the quest ends it */
    bool isCompleted(const std::string &plotId);

    std::string getQuestName(const std::string &plotId);
    std::string getEntryText(const std::string &plotId, int state);

    /** Adds a world entry; an entry with the same index is replaced. */
    void addWorldEntry(WorldEntry entry);
    /** @return true if an entry with the index was removed */
    bool removeWorldEntry(int index);
    /** @return true if an entry added by that string reference was removed */
    bool removeWorldEntryByStrref(int strref);
    void clearWorldEntries() { _worldEntries.clear(); }
    const std::vector<WorldEntry> &worldEntries() const { return _worldEntries; }

    /** The picture index SetJournalQuestEntryPicture gave a quest entry, -1 when none. */
    void setEntryPicture(const std::string &plotId, int pictureIndex);
    int entryPicture(const std::string &plotId) const;

    /** Quests in the order they were received. */
    const std::vector<Quest> &quests() const { return _quests; }

    /**
     * Set a listener invoked whenever addEntry changes quest state. Not
     * invoked by removeEntry or restoreEntry.
     */
    void setOnQuestChanged(std::function<void(const EntryChange &)> fn) { _onQuestChanged = std::move(fn); }

    const resource::JRL::Category *findCategory(const std::string &plotId);
    const resource::JRL::Entry *findEntry(const std::string &plotId, int state);

private:
    resource::IGffs &_gffs;
    resource::IStrings &_strings;

    bool _loaded {false};
    resource::JRL _jrl;
    std::vector<Quest> _quests;
    std::vector<WorldEntry> _worldEntries;
    std::map<std::string, int> _entryPictures;
    std::function<void(const EntryChange &)> _onQuestChanged;

    void ensureLoaded();
    void notifyQuestChanged(const resource::JRL::Category *category, const resource::JRL::Entry *entry);
    Quest *findQuest(const std::string &plotId);
    std::string resolveLocText(const resource::JRL::LocText &text);
};

} // namespace game

} // namespace reone
