# Guests at workbenches and lab stations

A co-op guest (Free or Story Mode) can use a workbench or lab station. The host runs everything;
the guest's screen only shows a view the host sends and sends requests back.

**Using one.** A placeable whose `OnUsed` script calls `ShowUpgradeScreen` is a workbench
(upgrades, plus crafting and breakdown of both materials); one that calls
`ShowChemicalUpgradeScreen` is a lab (crafting and breakdown with chemicals). The use is a
`UseObject` command; the guest walks there, then the host sends a `BenchView` (message 27). The
script is not run for the guest, so the host's own screen stays as it is. Other screen-opening
scripts (stores, galaxy map) are still refused. In this engine only K2 declares
`ShowChemicalUpgradeScreen`, and the host's own game does nothing for it (the routine is a
stub), so a lab is a guest-only screen for now.

**What the guest sees (`BenchView`).** Computed on the host from the guest's inventory: Free Mode
the guest's own bag and equipped items and its own components and chemicals; Story Mode the
shared inventory, the party's equipped items and the party's material.
- upgradeable items with their slots, what is installed, and the ids of the parts in the
  inventory that fit each slot;
- the inventory items that can be broken down, with the yield this guest's skill gives;
- the recipes, with whether the guest's skill reaches them and whether the material is enough.

**Commands** (target: the bench the guest has open; a command for another id, or with no bench
open, gets `BenchClosed`):

| command | param / item | rejects |
|---|---|---|
| `BenchUpgrade` | param: item id; item: part id (decimal text) | `ItemUnavailable` (not in the guest's items, part not held, not a number), `UpgradeIncompatible` (no slot takes it), `BenchClosed` (lab) |
| `BenchRemove` | param: item id; item: slot index | `ItemUnavailable`, `UpgradeIncompatible` (no such slot, empty, or a saber's colour slot), `BenchClosed` (lab) |
| `BenchCraft` | param: 0 components / 1 chemicals; item: recipe template | `ItemUnavailable` (no such recipe), `SkillTooLow`, `NotEnoughMaterial`, `BenchClosed` (bad material, or components at a lab) |
| `BenchBreak` | param: item id | `NotBreakable` (not in the bag, equipped, plot, not breakable, or not chemical at a lab) |
| `BenchClose` | | |

A rejected command changes nothing. An accepted one is carried out by the single-player rules
(`UpgradeBench`, `CraftingBench`) on the guest's inventory, then the view is sent again (to every
guest at a bench in Story Mode, since they share the inventory), and in Free Mode the guest's
character is sent so its machine's file follows. Skill checks use the guest's character.
K1/K2 upgrades and crafting cost no credits, so no credit check applies.

**Persistence.** A Free Mode guest's items keep their installed upgrades and a saber's blade
colour, and the guest keeps its own components and chemicals. `GuestCharacter` ends with an
optional extension for these; files saved before it decode with none.

## Not done

The guest's client has no workbench screen yet: `CoopSession` keeps the latest `BenchView` and
offers `clientBenchUpgrade/Remove/Craft/Break/Close`, and the status JSON lists it under `bench`,
but nothing draws it. A screen for it (reusing the upgrade GUI) is the next step.
