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

#include <cstdint>
#include <deque>
#include <vector>

namespace reone {

namespace game {

/**
 * Host, askToJoin: the join requests waiting for the host's Accept / Decline,
 * oldest first, and the one prompt that is up for them (docs/guest_scenes.md,
 * "Join requests"). One prompt at a time, in the order the requests came in;
 * a prompt something else took down unanswered (a screen change, another
 * pop-up) is counted and its request asked again first; an answer from
 * anywhere (the prompt, the Co-op screen, coopapprove) takes the request off.
 * Times are seconds on any steady clock.
 */
class CoopJoinQueue {
public:
    /**
     * A request joins the queue once; asking again keeps its place. `order` is
     * when the message that made the request reached the host (any increasing
     * count): requests the host takes on the same tick queue in that order,
     * not in the order it happens to visit its clients. 0 joins the back.
     * True if it is new.
     */
    bool request(uint32_t client, double now, uint64_t order = 0);
    /**
     * Answered, or the player left: off the queue. True if its prompt was up
     * (the caller takes the pop-up down when it is still that prompt).
     */
    bool remove(uint32_t client);
    bool contains(uint32_t client) const;

    /** The request to prompt for now: the oldest, while no prompt is up. 0 for none. */
    uint32_t next() const;
    /** The prompt for this request went up as the pop-up with this serial. */
    void shown(uint32_t client, uint64_t serial);
    /**
     * Is the prompt that went up still on screen? If not (the pop-up is hidden
     * or shows something else), it was taken down unanswered: counted, and its
     * request stays first to be asked again. Returns that client, 0 if none.
     */
    uint32_t checkPrompt(bool popupVisible, uint64_t popupSerial);

    /** The request whose prompt is up, 0 for none, and that pop-up's serial. */
    uint32_t prompted() const { return _prompted; }
    uint64_t promptSerial() const { return _serial; }
    /** Prompts taken down unanswered so far. */
    uint64_t lost() const { return _lost; }

    /**
     * Requests waiting longer than `after` seconds since they were made or
     * last reported, oldest first; each is marked reported at `now`.
     */
    std::vector<uint32_t> overdue(double now, double after);
    /** Seconds this request has waited, 0 if it is not queued. */
    double waited(uint32_t client, double now) const;

    /** The waiting clients, oldest first. */
    std::vector<uint32_t> clients() const;
    size_t size() const { return _requests.size(); }
    bool empty() const { return _requests.empty(); }

private:
    struct Request {
        uint32_t client {0};
        double since {0.0};
        double reported {0.0};
        uint64_t order {0};
    };
    std::deque<Request> _requests;
    uint32_t _prompted {0};
    uint64_t _serial {0};
    uint64_t _lost {0};
};

} // namespace game

} // namespace reone
