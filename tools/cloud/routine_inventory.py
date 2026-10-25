#!/usr/bin/env python3
"""Lists the NWScript routines the engine registers but does not implement.

A routine is "missing" when its function body throws RoutineNotImplementedException
(the engine logs "Routine not implemented" and returns a default). The inventory is
read from the sources only: src/libs/game/script/routine/impl/*.cpp.

    tools/cloud/routine_inventory.py            # write docs/dev/routines_missing.md
    tools/cloud/routine_inventory.py --check    # exit 1 if the doc is out of date
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IMPL = ROOT / "src/libs/game/script/routine/impl"
DOC = ROOT / "docs/dev/routines_missing.md"

REGISTRATION = re.compile(r'insert\(\s*(\d+)\s*,\s*"(\w+)"\s*,\s*(R_\w+)\s*,\s*\{([^}]*)\}\s*,\s*&(\w+)\s*\)')
REGISTER_FUNC = re.compile(r'void Routines::register(\w+?)(Kotor|Tsl)Routines\(\)\s*\{')


def function_bodies(text):
    """name -> body text of every `static Variable Name(...) { ... }`."""
    bodies = {}
    for m in re.finditer(r'static Variable (\w+)\(const std::vector<Variable> &args, const RoutineContext &ctx\)\s*\{', text):
        depth, i = 1, m.end()
        while depth and i < len(text):
            depth += {'{': 1, '}': -1}.get(text[i], 0)
            i += 1
        bodies[m.group(1)] = text[m.end():i]
    return bodies


def registrations(text):
    """[(game, index, name, ret, args, function)] from the register*Routines functions."""
    result = []
    starts = [(m.start(), m.group(2)) for m in REGISTER_FUNC.finditer(text)]
    for n, (start, kind) in enumerate(starts):
        end = starts[n + 1][0] if n + 1 < len(starts) else len(text)
        for r in REGISTRATION.finditer(text[start:end]):
            index, name, ret, args, fn = r.groups()
            result.append(("K1" if kind == "Kotor" else "K2", int(index), name, ret[2:].lower(),
                           [a.strip()[2:].lower() for a in args.split(",") if a.strip()], fn))
    return result


# Missing routines at the start of this work (the base branch), for before/after counts.
BASELINE = {"K1": (772, 138), "K2": (877, 203)}

# Routines whose behaviour could not be established from public documentation or from
# behaviour already in the repository. They stay missing until someone can settle the
# question; each needs a one-line reason.
NEEDS_RESEARCH = {
    "InsertString": "behaviour when nPosition is negative or past the end of the string is not documented",
    "IntToHexString": "exact format (prefix, width, case) is not documented",
    "TestStringAgainstPattern": "the pattern syntax (and how GetMatchedSubstring numbers its groups) is not documented",
    "GetMatchedSubstring": "depends on TestStringAgainstPattern",
    "GetMatchedSubstringsCount": "depends on TestStringAgainstPattern",
    "RandomName": "the name tables it draws from are not documented",
    "TouchAttackMelee": "attack-roll rules (and whether it consumes a round) are not documented",
    "TouchAttackRanged": "attack-roll rules (and whether it consumes a round) are not documented",
    "GetHasInventory": "which object types count as having an inventory is not documented",
    "GetItemACValue": "which item properties contribute to an item's AC value is not documented",
    "GetItemHasItemProperty": "needs the item-property model, which the engine does not keep per item yet",
    "GetCasterLevel": "needs the effect/spell source model of the running script",
    "GetMetaMagicFeat": "metamagic does not exist in KotOR; the routine's result is not documented",
    "SetCameraFacing": "camera control is presentation state that needs the camera model",
    "GetStartingLocation": "which entry point counts as the module's starting location is not documented",
    # Tranche 2
    "GetFactionWeakestMember": "what makes a member the weakest (challenge rating, level, hit points) is not documented",
    "GetFactionStrongestMember": "what makes a member the strongest (challenge rating, level, hit points) is not documented",
    "GetFactionMostDamagedMember": "whether damage is absolute or a fraction of maximum hit points is not documented",
    "GetFactionLeastDamagedMember": "whether damage is absolute or a fraction of maximum hit points is not documented",
    "GetFactionWorstAC": "depends on GetAC, which is not implemented, and on how ties resolve",
    "GetFactionBestAC": "depends on GetAC, which is not implemented, and on how ties resolve",
    "GetFactionMostFrequentClass": "how ties between classes resolve is not documented",
    "GetFactionGold": "whose gold a non-player faction has is not documented",
    "GetFactionLeader": "which member leads a non-player faction is not documented",
    "DoPlaceableObjectAction": "the PLACEABLE_ACTION_* values and their effects are not documented",
    "GetIsPlaceableObjectActionPossible": "the PLACEABLE_ACTION_* values and their effects are not documented",
    "AddJournalWorldEntry": "the journal keeps no world-entry model (separate from quest entries) to add to",
    "AddJournalWorldEntryStrref": "the journal keeps no world-entry model (separate from quest entries) to add to",
    "DeleteJournalWorldAllEntries": "the journal keeps no world-entry model (separate from quest entries) to delete from",
    "DeleteJournalWorldEntry": "the journal keeps no world-entry model (separate from quest entries) to delete from",
    "DeleteJournalWorldEntryStrref": "the journal keeps no world-entry model (separate from quest entries) to delete from",
    "RemoveEffectByID": "how a script obtains an effect id is not documented",
    "RemoveEffectByExactMatch": "what counts as an exact match between two effect values is not documented",
    "GetEncounterSpawnsMax": "whether 'spawns' counts spawn waves (Respawns) or creatures (MaxCreatures) is not documented",
    "SetEncounterSpawnsMax": "whether 'spawns' counts spawn waves (Respawns) or creatures (MaxCreatures) is not documented",
    "GetEncounterSpawnsCurrent": "whether 'spawns' counts spawn waves or creatures (NumberSpawned) is not documented",
    "SetEncounterSpawnsCurrent": "whether 'spawns' counts spawn waves or creatures (NumberSpawned) is not documented",
    "GetMovementRate": "the MOVEMENT_SPEED_* values it returns are not documented",
    "SetPlaceableIllumination": "the engine keeps no per-placeable illumination state to set",
    "GetPlaceableIllumination": "the engine keeps no per-placeable illumination state to read",
    "GetPCLevellingUp": "which creature counts as levelling up outside the level-up screen is not documented",
}

BEFORE_2 = {"K1": 107, "K2": 172}
BEFORE_3 = {"K1": 93, "K2": 158}

# Implemented in tranche 1 (queries and pure functions); checked against the source
# so the list cannot drift from reality.
TRANCHE_1 = [
    "fabs", "cos", "sin", "tan", "acos", "asin", "atan", "log", "pow", "sqrt",
    "GetStringUpperCase", "GetStringLowerCase", "StringToFloat",
    "VectorMagnitude", "VectorToAngle",
    "FeetToMeters", "YardsToMeters", "TurnsToSeconds",
    "GetDistanceBetweenLocations", "GetDistanceBetweenLocations2D",
    "GetLockKeyRequired", "GetLockKeyTag", "GetLockLockable", "GetLockUnlockDC", "GetLockLockDC",
    "GetIdentified", "SetIdentified",
    "PrintFloat", "PrintInteger", "PrintObject", "PrintVector",
]

TRANCHE_2 = [
    "GetEffectDurationType", "GetEffectSubType", "MagicalEffect", "SupernaturalEffect", "ExtraordinaryEffect",
    "GetMaxStealthXP", "GetStealthXPDecrement", "SetStealthXPDecrement",
    "GetReputation", "GetFirstFactionMember", "GetNextFactionMember",
    "GetFactionAverageLevel", "GetFactionAverageXP", "GetFactionAverageGoodEvilAlignment",
]

TRANCHE_3 = [
    "GetFortitudeSavingThrow", "GetReflexSavingThrow", "GetWillSavingThrow",
    "GetChallengeRating", "GetAC", "GetWeaponRanged", "SetGoodEvilValue",
    "GetNearestObjectToLocation", "GetNearestCreatureToLocation",
    "SetEncounterDifficulty", "GetEncounterDifficulty",
    "GetTransitionTarget", "SetAreaFogColor",
]

# Missing before tranche 4 (the routines tranche 4 settled, one way or the other).
BEFORE_4 = {"K1": 80, "K2": 145}

TRANCHE_4 = [
    "AddBonusForcePoints",
    "AddJournalWorldEntry",
    "AddJournalWorldEntryStrref",
    "AdjustCreatureAttributes",
    "AdjustCreatureSkills",
    "ChangeObjectAppearance",
    "CreatureFlourishWeapon",
    "CutsceneMove",
    "DeleteJournalWorldAllEntries",
    "DeleteJournalWorldEntry",
    "DeleteJournalWorldEntryStrref",
    "DetonateMine",
    "DisableMap",
    "DisplayDatapad",
    "DisplayMessageBox",
    "DoPlaceableObjectAction",
    "EnableRain",
    "EnableRendering",
    "EventActivateItem",
    "EventConversation",
    "ExploreAreaForPlayer",
    "ExportAllCharacters",
    "FloatingTextStrRefOnCreature",
    "FloatingTextStringOnCreature",
    "ForceHeartbeat",
    "GetAreaOfEffectCreator",
    "GetBonusForcePoints",
    "GetButtonMashCheck",
    "GetCasterLevel",
    "GetChemicalPieceValue",
    "GetChemicals",
    "GetCombatActionsPending",
    "GetEncounterSpawnsCurrent",
    "GetEncounterSpawnsMax",
    "GetFactionBestAC",
    "GetFactionGold",
    "GetFactionLeader",
    "GetFactionLeastDamagedMember",
    "GetFactionMostDamagedMember",
    "GetFactionMostFrequentClass",
    "GetFactionStrongestMember",
    "GetFactionWeakestMember",
    "GetFactionWorstAC",
    "GetFirstAttacker",
    "GetGoingToBeAttackedBy",
    "GetGoldPieceValue",
    "GetHasFeatEffect",
    "GetHasInventory",
    "GetHealTarget",
    "GetInfluence",
    "GetIsPartyLeader",
    "GetIsPlaceableObjectActionPossible",
    "GetIsPlayerMadeCharacter",
    "GetIsWeaponEffective",
    "GetIsXBox",
    "GetItemACValue",
    "GetItemComponent",
    "GetItemComponentPieceValue",
    "GetItemHasItemProperty",
    "GetLastAssociateCommand",
    "GetLastAttackMode",
    "GetLastAttackType",
    "GetLastConversation",
    "GetLastForfeitViolation",
    "GetLastItemEquipped",
    "GetLastLocked",
    "GetLastUnlocked",
    "GetLastWeaponUsed",
    "GetMatchedSubstring",
    "GetMatchedSubstringsCount",
    "GetMetaMagicFeat",
    "GetMovementRate",
    "GetNextAttacker",
    "GetObjectHeard",
    "GetObjectPersonalSpace",
    "GetOwnerDemolitionsSkill",
    "GetPCLevellingUp",
    "GetPlaceableIllumination",
    "GetRacialSubType",
    "GetRandomDestination",
    "GetSkillRankBase",
    "GetSpellCastItem",
    "GetStartingLocation",
    "GetStrRefSoundDuration",
    "GrantFeat",
    "GrantSpell",
    "HasLineOfSight",
    "InsertString",
    "IntToHexString",
    "IsInTotalDefense",
    "IsMeditating",
    "IsRunning",
    "IsStealthed",
    "ModifyFortitudeSavingThrowBase",
    "ModifyInfluence",
    "ModifyReflexSavingThrowBase",
    "ModifyWillSavingThrowBase",
    "PopUpDeathGUIPanel",
    "PopUpGUIPanel",
    "RandomName",
    "RemoveEffectByExactMatch",
    "RemoveEffectByID",
    "ResetCreatureAILevel",
    "SWMG_DestroyMiniGameObject",
    "SWMG_GetSwoopUpgrade",
    "SWMG_GetTrackPosition",
    "SWMG_PlayerApplyForce",
    "SWMG_SetFollowerPosition",
    "SWMG_SetJumpSpeed",
    "SendMessageToPC",
    "SetAreaTransitionBMP",
    "SetAssociateListenPatterns",
    "SetBonusForcePoints",
    "SetButtonMashCheck",
    "SetCameraFacing",
    "SetCameraMode",
    "SetCreatureAILevel",
    "SetDisableTransit",
    "SetEncounterSpawnsCurrent",
    "SetEncounterSpawnsMax",
    "SetFakeCombatState",
    "SetForceAlwaysUpdate",
    "SetForcePowerUnsuccessful",
    "SetForfeitConditions",
    "SetHealTarget",
    "SetInfluence",
    "SetInputClass",
    "SetJournalQuestEntryPicture",
    "SetKeepStealthInDialog",
    "SetOrientOnClick",
    "SetPlaceableIllumination",
    "SetTutorialWindowsEnabled",
    "ShowChemicalUpgradeScreen",
    "ShowDemoScreen",
    "ShowSwoopUpgradeScreen",
    "SoundObjectSetPosition",
    "SpawnMine",
    "SpeakOneLinerConversation",
    "StopRumblePattern",
    "SuppressStatusSummaryEntry",
    "TestStringAgainstPattern",
    "TouchAttackMelee",
    "TouchAttackRanged",
    "UnlockAllSongs",
    "WriteTimestampedLogEntry",
]

STUB_MARKER = re.compile(r"//\s*intentionally stubbed:\s*(.+)")

CATEGORIES = [
    ("minigames", r"^SWMG_"),
    ("effects", r"^(Effect|Magical|Supernatural|Extraordinary|Ignore|Set(Effect)|Get(Effect)|Apply|Remove|GetFirstEffect|GetNextEffect|Is(Effect)|Versus|Set(Is)?EffectCreator)"),
    ("math/string/utility", r"^(Random|Print|FloatToString|IntToString|StringTo|GetStringLength|GetStringUpperCase|GetStringLowerCase|GetStringRight|GetStringLeft|InsertString|GetSubString|FindSubString|Abs|fabs|cos|sin|tan|acos|asin|atan|log|pow|sqrt|Vector|Angle|GetDistance|Get(Vector|Squared)|IntToFloat|FloatToInt|ObjectToString|StringToInt|StringToFloat|GetStringByStrRef|SpeakString|Sort|GetSkillRankBasedOn|d\d|GetMatchedSubstring|GetMatchedSubstringsCount|Min|Max|Reflexive|GetLocation|Location|Get(Position|Facing|Area)From|GetCreatureTalentCount)"),
    ("party/global state", r"^(Get|Set)(Global|Local)|^(Get|Set)(PartyMember|NPC|Party|Player|Journal|Plot|Time|Calendar|Xp|XP|Galaxy|StealthXP|Credits|Reputation|Karma)|^(Add|Remove|Is)Party|^(Add|Remove|Show|Hide)Journal|^(Add|Remove)(Available)?NPC|^(Add|Remove)Quest|Journal|^Get(Gold|PCLevel|PC|Global)|^Delete(Global|Local)|^Set(Global|Local)|^(Get|Set|Is)(Available|Selectable|Solo)|^(Adjust)|^(Give|Take|Add)(Item|Xp|XP|Gold|Credits)|^Get(Is)?(Day|Night)|PartyAI|Alignment|Reputation|Faction|^ChangeFaction|^SetStandard"),
    ("GUI/presentation", r"(Cutscene|Camera|Movie|Fade|Music|Sound|Ambient|Voice|DisplayMessage|Dialog|Conversation|Screen|Cinematic|Gui|GUI|Interface|Bark|Popup|OpenStore|OpenInventory|Map|Tutorial|Hint|Show|Hide|Audio|Soundset|Lip|Pazaak|Terminal|Mini|Speak|Overlay|Highlight|Move(Camera|Fog)|Fog|Weather|Sky|Light|Particle|Visual|Animat|Pause|Wait)"),
    ("actions", r"^(Action|Assign|Delay|Clear|Cancel|Execute|Signal|Play(Animation)?|Jump|Take|Surrender|Equip|Unequip|Use|Cast|Attack|Do|Pick|Speak(OneLiner)?|Open|Close|Lock|Unlock|Rest|Follow|Move|Wait|Sit|Stand|Flee|Interact|Set(Commandable|Facing)|Get(Is)?Commandable|Pause|Resume|Surrender|Barter)"),
    ("object queries", r"^(Get|Is|Has|Object|Valid|Create|Destroy|Find|Count|Nearest|First|Next|Set)"),
]


def categorize(name):
    for label, pattern in CATEGORIES:
        if re.search(pattern, name):
            return label
    return "other"


def collect():
    entries = {}
    for source in sorted(IMPL.glob("*.cpp")):
        text = source.read_text()
        bodies = function_bodies(text)
        for game, index, name, ret, args, fn in registrations(text):
            body = bodies.get(fn, "")
            missing = "RoutineNotImplementedException" in body
            stub = STUB_MARKER.search(body)
            entries[(game, index)] = dict(game=game, index=index, name=name, ret=ret, args=args, fn=fn,
                                          file=source.name, missing=missing,
                                          stub=stub.group(1).strip() if stub else None)
    return entries


def render(entries):
    lines = ["# Missing NWScript routines", "",
             "Routines the engine registers but does not implement: calling one logs",
             "\"Routine not implemented\" and returns the default for its type. Generated by",
             "`tools/cloud/routine_inventory.py` from `src/libs/game/script/routine/impl/*.cpp`;",
             "regenerate it after changing a routine. A routine is missing when its function",
             "body throws `RoutineNotImplementedException`.", ""]
    for game, label in (("K1", "KotOR 1"), ("K2", "KotOR 2 (TSL)")):
        rows = sorted((e for e in entries.values() if e["game"] == game), key=lambda e: e["index"])
        missing = [e for e in rows if e["missing"]]
        before = BASELINE[game]
        lines += [f"## {label}: {len(missing)} of {len(rows)} registered routines missing", "",
                  f"Before tranche 1: {before[1]} of {before[0]} missing. Before tranche 2: {BEFORE_2[game]}. Before tranche 3: {BEFORE_3[game]}. Before tranche 4: {BEFORE_4[game]}. Now: {len(missing)}.", ""]
        by_cat = {}
        for e in missing:
            by_cat.setdefault(categorize(e["name"]), []).append(e)
        for cat in ["math/string/utility", "object queries", "effects", "actions", "party/global state",
                    "GUI/presentation", "minigames", "other"]:
            items = by_cat.get(cat, [])
            if not items:
                continue
            lines += [f"### {label}: {cat} ({len(items)})", "", "| # | routine | signature | status |", "|---|---|---|---|"]
            for e in items:
                signature = f"{e['ret']} {e['name']}({', '.join(e['args'])})"
                status = NEEDS_RESEARCH.get(e["name"])
                status = f"needs research: {status}" if status else "missing"
                lines.append(f"| {e['index']} | `{e['name']}` | `{signature}` | {status} |")
            lines.append("")
    done = [n for n in TRANCHE_1 if not any(e["name"] == n and e["missing"] for e in entries.values())]
    stale = [n for n in TRANCHE_1 if n not in done]
    lines += ["## Done: tranche 1", "",
              "Pure functions and queries whose behaviour is documented. Each has tests in",
              "`test/game/routines1.cpp`, run against K1 and K2.", "",
              ", ".join(f"`{n}`" for n in done), ""]
    if stale:
        lines += ["Listed in tranche 1 but still throwing: " + ", ".join(stale), ""]
    done2 = [n for n in TRANCHE_2 if not any(e["name"] == n and e["missing"] for e in entries.values())]
    stale2 = [n for n in TRANCHE_2 if n not in done2]
    lines += ["## Done: tranche 2", "",
              "Effect subtype and duration queries, stealth XP, and faction queries. Each has tests in",
              "`test/game/routines2.cpp`, run against K1 and K2.", "",
              ", ".join(f"`{n}`" for n in done2), "",
              "Behaviour chosen where the documentation is silent (see the tests):", "",
              "- An effect value starts out magical (subtype 8) and instant until it is applied. `MagicalEffect`,",
              "  `SupernaturalEffect` and `ExtraordinaryEffect` mark the value in place and return the same value,",
              "  so every script variable holding it sees the new subtype.",
              "- `GetEffectDurationType` and `GetEffectSubType` look an applied effect up among the objects of",
              "  the current area; an effect applied elsewhere reads as unapplied.",
              "- A faction's members are the creatures of the current area sharing its faction, dead ones",
              "  included, in area order. `bPCOnly` (the default) keeps the player and party members.",
              "  The averages are whole numbers (integer division) over all members, -1 for an invalid faction.",
              "- `GetReputation` answers like `GetFactionAverageReputation`: -1 for an invalid source, 0 for an",
              "  invalid target, otherwise the source faction's regard for the target's faction, clamped to 0..100.",
              ""]
    if stale2:
        lines += ["Listed in tranche 2 but still throwing: " + ", ".join(stale2), ""]
    done3 = [n for n in TRANCHE_3 if not any(e["name"] == n and e["missing"] for e in entries.values())]
    stale3 = [n for n in TRANCHE_3 if n not in done3]
    lines += ["## Done: tranche 3", "",
              "Creature and item queries, searches around a location, encounter difficulty, transition",
              "targets and area fog. Each has tests in `test/game/routines3.cpp`, run against K1 and K2.", "",
              ", ".join(f"`{n}`" for n in done3), "",
              "Behaviour chosen where the documentation is silent (see the tests):", "",
              "- The saving throw routines return the creature's current saving throw (class bonus plus effects),",
              "  `GetAC` its defense, `GetChallengeRating` its challenge rating; anything but a creature gives 0.",
              "- `GetNearestObjectToLocation` takes an exact object type like `GetNearestObject`; nNth counts from 1.",
              "- `GetNearestCreatureToLocation` takes the same criteria as `GetNearestCreature`; reputation and",
              "  perception criteria need a creature to look from, so they never match around a bare location.",
              "  `Area::getNearestCreatureToLocation` used to match nothing at all; it now ignores a missing target.",
              "- `GetTransitionTarget` finds the waypoint named by a door's or trigger's LinkedTo tag in the",
              "  current area; a transition into another module has none.",
              "- `SetAreaFogColor` clamps each component to 0..1 and pushes the fog to the scene.",
              ""]
    if stale3:
        lines += ["Listed in tranche 3 but still throwing: " + ", ".join(stale3), ""]
    done4 = [n for n in TRANCHE_4 if not any(e["name"] == n and e["missing"] for e in entries.values())]
    stubs = {}
    for e in entries.values():
        if e["stub"]:
            stubs.setdefault(e["name"], {"reason": e["stub"], "games": set(), "sig": f"{e['ret']} {e['name']}({', '.join(e['args'])})"})["games"].add(e["game"])
    implemented4 = [n for n in done4 if n not in stubs]
    lines += ["## Done: tranche 4", "",
              "Everything that was still missing, including the earlier \"needs research\" items that public NWScript",
              "documentation settles. Tests are in `test/game/routines4.cpp`, run against K1 and K2.", "",
              f"### Implemented ({len(implemented4)})", "",
              ", ".join(f"`{n}`" for n in implemented4), "",
              f"### Intentionally stubbed ({len(stubs)})", "",
              "These return a documented default and never throw; the reason is the engine having no model",
              "for the routine to act on. Each reason is the `intentionally stubbed:` comment in the source.", "",
              "| routine | games | signature | reason |", "|---|---|---|---|"]
    for name in sorted(stubs):
        info = stubs[name]
        lines.append(f"| `{name}` | {'+'.join(sorted(info['games']))} | `{info['sig']}` | {info['reason']} |")
    lines += ["", "### Tranche 5: stubs given a model", "",
              "`GetLastItemEquipped` answers the item most recently put on by any creature (`Creature::equip` records it",
              "on the module; it is invalid once the item is gone from the world). `GetSpellCastItem` answers the item",
              "whose Activate Item property cast the spell: `CastSpellAtObjectAction` passes it to the impact script as",
              "`ArgKind::SpellCastItem`. Tests are in `test/game/routines4.cpp`.",
              "The remaining stubs need a model the engine lacks (stances, attack records, area-of-effect casters,",
              "screens, controller input), none of which public documentation lets a routine invent."]
    lines += ["", "### Still unknown", "",
              "Nothing is missing. Behaviour the documentation leaves open is stated next to each routine in the",
              "tests; the choices that deserve a second look are:", "",
              "- Pattern matching (`TestStringAgainstPattern`) is anchored at the start of the text and ignores text",
              "  after the pattern's last piece, because the documented example needs that.",
              "- `IntToHexString` gives `0x` and eight lower-case digits (the documented example `0x0000007b`).",
              "- Several setters (`SetHealTarget`, `SetCreatureAILevel`, `SetBonusForcePoints`, `SetFakeCombatState`,",
              "  `SetOrientOnClick`, `SetForceAlwaysUpdate`, `SetDisableTransit`, `SetForfeitConditions`,",
              "  `SetKeepStealthInDialog`, `EnableRain`, `SetForcePowerUnsuccessful`, `SetButtonMashCheck`) store their",
              "  value for the matching getter, but nothing in the engine reads it yet.",
              "- `AddJournalWorldEntry` and its siblings keep world notes in the journal; no screen lists them and saves",
              "  do not carry them. `ModifyXSavingThrowBase` changes are not saved either.",
              "- `GetItemComponentPieceValue` and `GetChemicalPieceValue` answer 20, the credits one workbench piece stands for.",
              "- The `PLACEABLE_ACTION_*` values are taken as USE 0, UNLOCK 1, BASH 2, KNOCK 3.",
              ""]
    lines += ["## Notes for later tranches", "",
              "- `GetDistanceBetweenLocations` and `...2D` are documented to give -1.0 for locations in",
              "  different areas; a `Location` carries no area, so they measure every pair as one area.",
              "- Existing routines that look wrong next to the documentation (not changed here):",
              "  `AngleToVector` takes radians although scripts pass degrees (compare `GetFacing`, which",
              "  returns degrees); `RoundsToSeconds` divides by 6 where a round is 6 seconds;",
              "  `HoursToSeconds` is declared to return a float but returns an int.",
              "- Regenerate this file with `tools/cloud/routine_inventory.py`; `--check` fails if stale.", ""]
    return "\n".join(lines).rstrip() + "\n"


def main():
    entries = collect()
    text = render(entries)
    if "--check" in sys.argv:
        sys.exit(0 if DOC.exists() and DOC.read_text() == text else 1)
    if "--counts" in sys.argv:
        for game in ("K1", "K2"):
            rows = [e for e in entries.values() if e["game"] == game]
            print(game, "registered", len(rows), "missing", sum(e["missing"] for e in rows))
        return
    DOC.parent.mkdir(exist_ok=True)
    DOC.write_text(text)
    print("wrote", DOC)


if __name__ == "__main__":
    main()
