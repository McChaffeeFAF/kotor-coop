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

#include "reone/game/guestbenchscreen.h"

#include <algorithm>

#include <boost/algorithm/string.hpp>

using namespace reone::net;

namespace reone {

namespace game {

static constexpr size_t kMaxTrackedSeqs = 32;

void GuestBenchScreen::setView(const BenchView &view) {
    _view = view;
    ++_revision;
}

void GuestBenchScreen::clearStandIns() {
    _hostOfLocal.clear();
    _localOfHost.clear();
}

void GuestBenchScreen::mapStandIn(uint32_t localId, uint32_t hostId) {
    _hostOfLocal[localId] = hostId;
    _localOfHost[hostId] = localId;
}

uint32_t GuestBenchScreen::hostId(uint32_t localId) const {
    auto it = _hostOfLocal.find(localId);
    return it != _hostOfLocal.end() ? it->second : 0;
}

uint32_t GuestBenchScreen::localId(uint32_t hostId) const {
    auto it = _localOfHost.find(hostId);
    return it != _localOfHost.end() ? it->second : 0;
}

std::vector<GuestBenchScreen::StandIn> GuestBenchScreen::standIns() const {
    std::vector<StandIn> result;
    auto has = [&result](uint32_t host) {
        return std::any_of(result.begin(), result.end(), [host](const StandIn &entry) { return entry.host == host; });
    };
    for (const auto &item : _view.items) {
        if (item.id && !item.resref.empty() && !has(item.id)) {
            result.push_back({item.id, item.resref, 1, item.upgrades, item.modelVariation});
        }
    }
    for (const auto &part : _view.parts) {
        if (part.id && !part.resref.empty() && !has(part.id)) {
            result.push_back({part.id, part.resref, std::max<uint16_t>(1, part.stack), 0, -1});
        }
    }
    for (const auto &entry : _view.breakdown) {
        if (entry.id && !entry.resref.empty() && !has(entry.id)) {
            result.push_back({entry.id, entry.resref, std::max<uint16_t>(1, entry.stack), 0, -1});
        }
    }
    return result;
}

const BenchItem *GuestBenchScreen::item(uint32_t hostId) const {
    for (const auto &entry : _view.items) {
        if (entry.id == hostId) {
            return &entry;
        }
    }
    return nullptr;
}

const BenchBreakdown *GuestBenchScreen::breakdown(uint32_t hostId) const {
    for (const auto &entry : _view.breakdown) {
        if (entry.id == hostId) {
            return &entry;
        }
    }
    return nullptr;
}

const BenchRecipe *GuestBenchScreen::recipe(const std::string &resref, int material) const {
    for (const auto &entry : _view.recipes) {
        if (entry.material == material && boost::iequals(entry.resref, resref)) {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<const BenchItem *> GuestBenchScreen::items(int category) const {
    std::vector<const BenchItem *> result;
    if (!upgrades()) {
        return result;
    }
    for (const auto &entry : _view.items) {
        if (category == 0 || entry.category == category) {
            result.push_back(&entry);
        }
    }
    return result;
}

std::vector<BenchRecipe> GuestBenchScreen::recipes() const {
    std::vector<BenchRecipe> result;
    for (const auto &entry : _view.recipes) {
        if (!lab() || entry.material == 1) {
            result.push_back(entry);
        }
    }
    return result;
}

std::vector<BenchBreakdown> GuestBenchScreen::breakdowns() const {
    std::vector<BenchBreakdown> result;
    for (const auto &entry : _view.breakdown) {
        if (!lab() || entry.material == 1) {
            result.push_back(entry);
        }
    }
    return result;
}

std::vector<BenchRequest> GuestBenchScreen::assemble(uint32_t hostItem, const std::vector<StagedBenchSlot> &staged) const {
    std::vector<BenchRequest> removals;
    std::vector<BenchRequest> installs;
    const BenchItem *entry = upgrades() ? item(hostItem) : nullptr;
    if (!entry) {
        return {};
    }
    size_t count = std::min(entry->slots.size(), staged.size());
    for (size_t i = 0; i < count; ++i) {
        const BenchSlot &slot = entry->slots[i];
        const StagedBenchSlot &now = staged[i];
        if (slot.color) {
            if (now.part) {
                installs.push_back({BenchRequest::Kind::Upgrade, hostItem, now.part, static_cast<int>(i), "", 0});
            }
            continue;
        }
        if (now.installedRow == slot.installedRow) {
            continue;
        }
        if (slot.installedRow >= 0) {
            removals.push_back({BenchRequest::Kind::Remove, hostItem, 0, static_cast<int>(i), "", 0});
        }
        if (now.installedRow >= 0 && now.part) {
            installs.push_back({BenchRequest::Kind::Upgrade, hostItem, now.part, static_cast<int>(i), "", 0});
        }
    }
    removals.insert(removals.end(), installs.begin(), installs.end());
    return removals;
}

BenchRequest GuestBenchScreen::craft(const BenchRecipe &recipe) const {
    BenchRequest request;
    request.kind = BenchRequest::Kind::Craft;
    request.resref = recipe.resref;
    request.material = recipe.material == 1 ? 1 : 0;
    return request;
}

BenchRequest GuestBenchScreen::breakDown(uint32_t hostItem) const {
    BenchRequest request;
    request.kind = BenchRequest::Kind::Break;
    request.item = hostItem;
    return request;
}

void GuestBenchScreen::track(uint32_t seq) {
    if (!seq) {
        return;
    }
    _seqs.push_back(seq);
    if (_seqs.size() > kMaxTrackedSeqs) {
        _seqs.erase(_seqs.begin());
    }
}

bool GuestBenchScreen::tracks(uint32_t seq) const {
    return seq && std::find(_seqs.begin(), _seqs.end(), seq) != _seqs.end();
}

void GuestBenchScreen::reject(uint32_t seq, CommandReject reason) {
    if (!tracks(seq) || reason == CommandReject::None) {
        return;
    }
    _rejection = rejectionText(reason);
    ++_revision;
}

std::string GuestBenchScreen::rejectionText(CommandReject reason) {
    switch (reason) {
    case CommandReject::None:
        return "";
    case CommandReject::BenchClosed:
        return "The bench cannot do that";
    case CommandReject::UpgradeIncompatible:
        return "That upgrade does not fit there";
    case CommandReject::NotEnoughMaterial:
        return "Not enough material";
    case CommandReject::SkillTooLow:
        return "Your skill is too low to make that";
    case CommandReject::NotBreakable:
        return "That cannot be broken down";
    case CommandReject::ItemUnavailable:
        return "You no longer have that item";
    case CommandReject::RateLimited:
        return "Too many requests: try again";
    default:
        return std::string("The host refused: ") + commandRejectName(reason);
    }
}

void GuestBenchScreen::reset() {
    _view = BenchView();
    _view.open = false;
    ++_revision;
    clearStandIns();
    _seqs.clear();
    _rejection.clear();
}

} // namespace game

} // namespace reone
