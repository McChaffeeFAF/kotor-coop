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

## The guest's screen

The guest's client draws the view on the single-player upgrade screen (`UpgradeGUI`, the
`ShowUpgradeScreen` screens: category page, items page, slots page, and the crafting page).
Each `BenchView` rebuilds local stand-in items from this install's templates (with the host's
upgrade bits and blade colour) in a hidden store shell, and the screen's `UpgradeBench` and
`CraftingBench` work on that holder, so the guest picks and stages slots exactly as in single
player. Nothing is applied locally:

- **Assemble** turns the staged slots into requests (`GuestBenchScreen::assemble`): removals
  first (a taken slot offers no parts until it is empty), then installs by the part's host id.
  A saber's colour slot is swapped by installing a crystal, never removed.
- **Create / Break Down** on the crafting page send `BenchCraft` / `BenchBreak`; the rows,
  costs, yields and the skilled/affordable marks come from the view (names from this install).
- A **lab** opens straight on the crafting page (Back closes it); K1 has no lab placeable or lab
  GUI, so this page is the lab's screen in both games.
- A slot's icon and the first row of its list both show what the slot holds as staged
  (`UpgradeBench::installedTemplate`); for a saber's colour slot that is the current colour
  crystal, listed before the crystals it could take (single player too).
- K1's slots page (single player too) fits LB_ITEMS between the item name / "Upgrades:" /
  slot-name lines and the slot buttons (`fitListBetween`; a saber's list stops above "Color
  Crystal"), and LB_DESC, authored without a proto item, borrows LB_DESC_LS's
  (`ListBox::adoptProtoItem`), so a non-saber item's description shows.
- A new view redraws the page the guest is on (the slots page reopens the same item by host id).
- **Back/Close** sends `BenchClose`; a closed view from the host takes the screen down.
- A **rejected** bench request shows the host's reason at the top of the page's description, in
  the feedback message log and as a notice ("That upgrade does not fit there", "Not enough
  material", "Your skill is too low to make that", ...).

A placeable whose `OnUsed` calls `ShowUpgradeScreen` is a bench use even when it also has a
conversation: K1's Taris apartment workbench talks once before its script opens the screen.

Harness: `coopbench [state|item <tpl>|install <part tpl>|remove <slot>|assemble|craft <tpl>|break
<tpl>|close]` drives the guest's screen as its buttons do (`send remove|upgrade <item> <n>` sends a
raw request, as a stale screen would). The host's `netstatus` lists each guest's `bench` and its
`upgraded` items. An integration probe (not included) runs it end to end (Free Mode, tar_m02af).

## Not done

- The host picks the slot for `BenchUpgrade` (the first one whose candidates hold the part). For
  items whose slots take different kinds this is the slot the guest staged; for two empty slots
  of the same kind (a K1 saber's power crystals) the part may land in the other one.
- Labs are covered by unit tests only: no K2 co-op run reaches a lab station yet, and K1 has none.
- Crafting needs the K2 systems merge on the host (otherwise the view has no recipes and the
  guest's workbench shows no crafting rows).
- K1 upgrade slots come from each item's own upgrade properties (properties carrying an
  `UpgradeType` row of upgrade.2da), as in single player: K1 has no base-item column for them.
  Only upgradeable variants carry them (e.g. `g_w_blstrpstl005`, `g_w_blstrrfl005`,
  `g_w_vbroshort04`, `g_a_class8005`, the sabers); plain `g_w_blstrpstl001` has none and an
  upgrade in it would change nothing. The probe uses the 005 blaster and a saber.
