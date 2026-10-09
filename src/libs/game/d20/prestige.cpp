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

#include "reone/game/d20/prestige.h"

#include <algorithm>

namespace reone {

namespace game {

namespace prestige {

static constexpr int kAlignmentNeutral = 1;
static constexpr int kAlignmentLight = 2;
static constexpr int kAlignmentDark = 3;

AlignmentBand alignmentBand(int goodEvil) {
    if (goodEvil < kNeutralFrom) {
        return AlignmentBand::Dark;
    }
    if (goodEvil < kLightFrom) {
        return AlignmentBand::Neutral;
    }
    return AlignmentBand::Light;
}

int alignmentConstant(AlignmentBand band) {
    switch (band) {
    case AlignmentBand::Dark:
        return kAlignmentDark;
    case AlignmentBand::Light:
        return kAlignmentLight;
    default:
        return kAlignmentNeutral;
    }
}

bool isPrestigeClass(ClassType type) {
    switch (type) {
    case ClassType::JediWeaponMaster:
    case ClassType::JediMaster:
    case ClassType::JediWatchman:
    case ClassType::SithMarauder:
    case ClassType::SithLord:
    case ClassType::SithAssassin:
        return true;
    default:
        return false;
    }
}

std::vector<ClassType> classesForBand(AlignmentBand band) {
    static const std::vector<ClassType> kLight {
        ClassType::JediWeaponMaster, ClassType::JediMaster, ClassType::JediWatchman};
    static const std::vector<ClassType> kDark {
        ClassType::SithMarauder, ClassType::SithLord, ClassType::SithAssassin};
    switch (band) {
    case AlignmentBand::Light:
        return kLight;
    case AlignmentBand::Dark:
        return kDark;
    default: {
        std::vector<ClassType> all(kLight);
        all.insert(all.end(), kDark.begin(), kDark.end());
        return all;
    }
    }
}

static bool isJediClass(ClassType type) {
    return type == ClassType::JediGuardian ||
           type == ClassType::JediConsular ||
           type == ClassType::JediSentinel;
}

std::vector<ClassType> offeredClasses(const Facts &facts) {
    if (!facts.k2Merged || !facts.playerCharacter || !facts.revealed) {
        return {};
    }
    if (static_cast<int>(facts.classes.size()) >= kMaxClassSlots) {
        return {};
    }
    bool jedi = std::any_of(facts.classes.begin(), facts.classes.end(), isJediClass);
    bool prestige = std::any_of(facts.classes.begin(), facts.classes.end(), isPrestigeClass);
    if (!jedi || prestige) {
        return {};
    }
    int goodEvil = facts.revealGoodEvil.value_or(facts.goodEvil);
    return classesForBand(alignmentBand(goodEvil));
}

static bool isBaseClass(ClassType type) {
    return type == ClassType::Soldier || type == ClassType::Scout ||
           type == ClassType::Scoundrel || isJediClass(type);
}

NewClassVerdict checkNewClass(const Facts &facts, ClassType wanted) {
    bool prestigeWanted = isPrestigeClass(wanted);
    if (!prestigeWanted && !isBaseClass(wanted)) {
        return NewClassVerdict::Invalid;
    }
    if (std::find(facts.classes.begin(), facts.classes.end(), wanted) != facts.classes.end()) {
        return NewClassVerdict::Invalid;
    }
    bool hasPrestige = std::any_of(facts.classes.begin(), facts.classes.end(), isPrestigeClass);
    if (!prestigeWanted) {
        // A second base class: at most two base classes, none after a prestige class.
        return facts.classes.size() >= 2 || hasPrestige ? NewClassVerdict::SlotsFull : NewClassVerdict::Allowed;
    }
    if (!facts.k2Merged || !facts.playerCharacter || !facts.revealed) {
        return NewClassVerdict::Locked;
    }
    bool jedi = std::any_of(facts.classes.begin(), facts.classes.end(), isJediClass);
    if (!jedi) {
        return NewClassVerdict::Locked;
    }
    if (hasPrestige || static_cast<int>(facts.classes.size()) >= kMaxClassSlots) {
        return NewClassVerdict::SlotsFull;
    }
    auto offered = offeredClasses(facts);
    return std::find(offered.begin(), offered.end(), wanted) != offered.end()
               ? NewClassVerdict::Allowed
               : NewClassVerdict::Alignment;
}

bool grantsGreyFeat(int goodEvilAtSelection) {
    return alignmentBand(goodEvilAtSelection) == AlignmentBand::Neutral;
}

int greySavingThrowBonus(bool hasGrey, int goodEvil) {
    return hasGrey && alignmentBand(goodEvil) == AlignmentBand::Neutral ? 1 : 0;
}

bool waivesOppositeAlignmentSurcharge(bool k2Merged, int goodEvil) {
    return k2Merged && alignmentBand(goodEvil) == AlignmentBand::Neutral;
}

} // namespace prestige

} // namespace game

} // namespace reone
