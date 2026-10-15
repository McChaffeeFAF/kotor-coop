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
#include <map>
#include <string>
#include <vector>

#include "reone/net/protocol.h"

namespace reone {

namespace game {

/** One request the guest's bench screen sends the host (CoopSession::clientBench*). */
struct BenchRequest {
    enum class Kind {
        Upgrade, // item, part
        Remove,  // item, slot
        Craft,   // resref, material
        Break    // item
    };

    Kind kind {Kind::Upgrade};
    uint32_t item {0}; // host id
    uint32_t part {0}; // host id
    int slot {-1};
    std::string resref;
    int material {0}; // 0 components, 1 chemicals

    bool operator==(const BenchRequest &other) const {
        return kind == other.kind && item == other.item && part == other.part && slot == other.slot &&
               resref == other.resref && material == other.material;
    }
};

/** What the guest staged in one slot of the item on the upgrade page. */
struct StagedBenchSlot {
    int installedRow {-1}; // upgrade.2da row staged in it, or -1
    uint32_t part {0};     // host id of the part staged into it now (0: none staged)
};

/**
 * The guest's workbench or lab screen, without the GUI: the host's BenchView, the local
 * stand-in items the upgrade screen shows for it (host id <-> local id), and the requests that
 * turn what the guest staged into host commands. Nothing here changes an inventory: the host
 * does, then sends a new view.
 */
class GuestBenchScreen {
public:
    void setView(const net::BenchView &view);
    const net::BenchView &view() const { return _view; }
    /** A view arrived and is open. */
    bool open() const { return _view.bench != 0 && _view.open; }
    bool lab() const { return _view.kind == net::BenchKind::Lab; }
    /** Upgrades: workbenches only. */
    bool upgrades() const { return !lab(); }
    /** Crafting and breakdown: anything to make or break down (always at a lab). */
    bool crafting() const { return lab() || !_view.recipes.empty() || !_view.breakdown.empty(); }
    /** Bumped by every view: the screen redraws when it changes. */
    uint32_t revision() const { return _revision; }

    // Stand-ins: the local item drawn for a host item.
    void clearStandIns();
    void mapStandIn(uint32_t localId, uint32_t hostId);
    uint32_t hostId(uint32_t localId) const;
    uint32_t localId(uint32_t hostId) const;
    /** Every host item the stand-ins hold, once each: items, parts, then breakdown. */
    struct StandIn {
        uint32_t host {0};
        std::string resref;
        uint16_t stack {1};
        uint32_t upgrades {0};
        int modelVariation {-1};
    };
    std::vector<StandIn> standIns() const;

    const net::BenchItem *item(uint32_t hostId) const;
    const net::BenchBreakdown *breakdown(uint32_t hostId) const;
    const net::BenchRecipe *recipe(const std::string &resref, int material) const;
    /** Upgradeable items in a category (0 all), in the view's order. */
    std::vector<const net::BenchItem *> items(int category) const;

    /** The recipes to list, in the view's order: at a lab only the chemical ones. */
    std::vector<net::BenchRecipe> recipes() const;
    /** The items to list for breakdown: at a lab only the chemical ones. */
    std::vector<net::BenchBreakdown> breakdowns() const;

    /**
     * The requests that make the host's item look like the staged slots: removals first (a
     * taken slot has no candidates until it is empty), then installs. A colour slot is never
     * removed, only swapped by installing a crystal. Slots past the view's are ignored.
     */
    std::vector<BenchRequest> assemble(uint32_t hostItem, const std::vector<StagedBenchSlot> &staged) const;
    BenchRequest craft(const net::BenchRecipe &recipe) const;
    BenchRequest breakDown(uint32_t hostItem) const;

    // Rejections: the host's reason for the last refused request, shown on the screen.
    void track(uint32_t seq);
    bool tracks(uint32_t seq) const;
    void reject(uint32_t seq, net::CommandReject reason);
    const std::string &rejection() const { return _rejection; }
    void clearRejection() { _rejection.clear(); }
    static std::string rejectionText(net::CommandReject reason);

    void reset();

private:
    net::BenchView _view;
    uint32_t _revision {0};
    std::map<uint32_t, uint32_t> _hostOfLocal;
    std::map<uint32_t, uint32_t> _localOfHost;
    std::vector<uint32_t> _seqs;
    std::string _rejection;
};

} // namespace game

} // namespace reone
