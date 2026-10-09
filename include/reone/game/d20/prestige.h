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

#include "../types.h"

#include <optional>
#include <vector>

namespace reone {

namespace game {

/**
 * K2 prestige classes in the K1 campaign (design note k2_merge,
 * "Prestige classes"; ADDON_KORRIBAN.md "Prestige classes in K1").
 */
namespace prestige {

/// The engine's good/evil bands, as GetAlignmentGoodEvil reports them
/// (K2 behaviour): GoodEvil below 41 is dark, 60 and above is light, and
/// 41-59 is neutral.
enum class AlignmentBand {
    Dark,
    Neutral,
    Light
};

constexpr int kNeutralFrom = 41;
constexpr int kLightFrom = 60;

AlignmentBand alignmentBand(int goodEvil);

/// The ALIGNMENT_* constant a band is reported as (nwscript.nss).
int alignmentConstant(AlignmentBand band);

bool isPrestigeClass(ClassType type);

/// Light: Weapon Master, Jedi Master, Watchman. Dark: Marauder, Sith Lord,
/// Assassin. Neutral: all six (owner decision).
std::vector<ClassType> classesForBand(AlignmentBand band);

/// K1's own "the PC knows it is Revan" plot flag. The Leviathan reveal
/// conversation (lev40_darthmala2) sets it at its end; K1 companions' "talk
/// about Revan" branches test it.
constexpr char kRevealGlobal[] = "Lev_MalDream";

/// Ours: the PC's GoodEvil at the moment the reveal flag was first set, plus
/// one (0: not recorded). A global number, so saves carry it.
constexpr char kRevealAlignmentGlobal[] = "K2M_RevealGoodEvil";

/// Most class slots a creature can have with the K2 systems merged: base,
/// Jedi and prestige.
constexpr int kMaxClassSlots = 3;

struct Facts {
    bool k2Merged {false};
    bool playerCharacter {false};
    bool revealed {false};
    std::optional<int> revealGoodEvil; // GoodEvil when the reveal happened
    int goodEvil {50};                 // now
    std::vector<ClassType> classes;    // in slot order
};

/**
 * The prestige classes the PC may take at a level-up, in class order: none
 * unless the K2 systems are merged, the PC has a Jedi class and no prestige
 * class yet, has a free class slot and the reveal has happened. The band is
 * the alignment at the reveal (the current one if it was not recorded).
 */
std::vector<ClassType> offeredClasses(const Facts &facts);

/// Why a level-up into a class the creature does not have yet is refused.
enum class NewClassVerdict {
    Allowed,
    Invalid,    // not a class a level-up can take
    Locked,     // a prestige class not unlocked (no reveal, no Jedi class, K1 without the merge)
    Alignment,  // a prestige class of the other alignment band
    SlotsFull   // no free class slot, or a second base class after a prestige class
};

/**
 * Whether `wanted` can be taken as a new class at a level-up. A second base
 * class (Soldier, Scout, Scoundrel or a Jedi class) takes the second slot
 * and needs no prestige class yet; a prestige class follows offeredClasses()
 * and takes the third slot. Single-player rules are unchanged: this is the
 * check a co-op host runs for a guest's choice.
 */
NewClassVerdict checkNewClass(const Facts &facts, ClassType wanted);

/// The Grey feat (ours): granted at prestige selection when the PC is in
/// the neutral band at that moment.
bool grantsGreyFeat(int goodEvilAtSelection);

/// The label of our Grey row in the merged feat.2da.
constexpr char kGreyFeatLabel[] = "K2M_GREY";

/// Grey: +1 to every saving throw while the character stays in the neutral
/// band.
int greySavingThrowBonus(bool hasGrey, int goodEvil);

/// The neutral bonus (a): a neutral-band player character pays no
/// opposite-alignment surcharge from forceadjust.2da.
bool waivesOppositeAlignmentSurcharge(bool k2Merged, int goodEvil);

} // namespace prestige

} // namespace game

} // namespace reone
