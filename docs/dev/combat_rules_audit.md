# Combat and effects rules audit

An audit of the d20 rules layer (`src/libs/game`: saving throws, the damage
pipeline, effect lifetimes and stacking, Force costs) against the public d20 /
KotOR rules, with a synthetic-table test for each rule. The new tests are in
`test/game/rulesaudit.cpp` (`RulesTest.*`); older tests that already pin a rule
are named in the last column.

Status: **right** (matched the rule; test added or found), **fixed** (was
wrong; fixed here), **open** (could not be settled from public documentation;
left as is and listed below).

## 1. Saving throws

| rule | status | tests |
|---|---|---|
| Base save = the class's save table at the class level, per class | right | `base_saves_come_from_the_class_table_at_the_class_level` |
| Several classes: base saves add up class by class | right | `multiclass_base_saves_add_up_class_by_class` |
| Fortitude + Constitution, Reflex + Dexterity, Will + Wisdom modifiers; modifier = floor((score - 10) / 2), so 9 gives -1 | right | `each_save_adds_its_own_ability_modifier_rounded_down` |
| A save effect changes only the save (or all) and the type (or all) it names; decreases subtract | right | `a_saving_throw_effect_changes_only_the_save_and_type_it_names`, `C08bTest.saving_throw_modifiers_apply_and_decreases_respect_immunity` |
| Bonus total capped at +20, penalties not capped | right | `saving_throw_bonuses_from_effects_are_capped_but_penalties_are_not` |
| One spell cast on a creature again does not stack its save bonus; other spells, items and scripts do; a stronger recast counts at its best value | **fixed**: save effects were summed | `one_spell_cast_twice_does_not_stack_...`, `a_stronger_recast_...` |
| Roll = d20 + save against the difficulty class | right | `saving_throw_roll_is_d20_plus_the_save_...` |
| Immunity to the effect a save would gate: an Immunity effect refuses the effect when applied and leaves other effects alone | right | `immunity_refuses_the_effect_it_names_...`, `C08Test.paralyze_sleep_and_horror_respect_immunity...` |
| Force-power saves against a Force power add the K2 form modifiers | right (covered with the forms) | `forms` tests |

## 2. Damage pipeline

Order of application, as the implementation does it and the NWN/KotOR
documentation describes it: energy shield, then percentage immunity, then flat
resistance, then damage reduction, never below zero.

| rule | status | tests |
|---|---|---|
| Damage immunity is a percentage per type; 100% is none; negative immunity is vulnerability; the least immunity of the types in a packet governs | right | `damage_immunity_is_a_percentage_of_each_type`, `CompositeDamage.least_immunity_governs_...` |
| Immunities of one type add up, to at most total immunity | right | `damage_immunities_of_one_type_add_up_...` |
| Damage resistance: a flat amount off each hit of its type; the best one counts, they do not add up | right | `damage_resistance_takes_a_flat_amount_...`, `CompositeDamage.resistance_applies_once_...` |
| Damage reduction x/+n: physical damage only; bypassed by a weapon of power n or more; the best one counts | right | `damage_reduction_is_bypassed_...`, `CompositeDamage.physical_component_enables_one_aggregate_reduction` |
| Order immunity, resistance, reduction (a case where every other order differs) | right | `mitigation_is_applied_in_the_order_...` |
| A resistance or reduction with a limit wears out and is removed | right (see open question 2) | `a_resistance_with_a_limit_wears_out_and_goes`, `C08bTest.a_linked_damage_resistance_wears_its_limit_down_...` |
| Energy shields take a hit first; a spent shield collapses | right | `C08ShieldTest.*` |
| Plot creatures take no damage | right | `plot_creatures_take_no_damage` |
| Damage lowers hit points; death at zero; healing stops at the maximum and does not revive | right | `applied_damage_lowers_hit_points_and_kills_at_zero`, `healing_never_passes_the_maximum_...` |
| Temporary hit points are a pool on top of the maximum: current may exceed the maximum, healing fills to maximum + pool, losing the pool can kill | right | `temporary_hit_points_are_a_separate_pool_...`, `C08bTest.temporary_hit_points_*` |
| Difficulty scales only party damage | right | `CombatDifficulty.*` |

## 3. Effect lifetimes and stacking

| rule | status | tests |
|---|---|---|
| A temporary effect ends when its duration has run out; a permanent one stays; an instant one is not kept | right | `a_temporary_effect_ends_...`, `an_instant_effect_is_not_kept`, `EffectInstance.should_expire_temporary_runtime_effects` |
| One spell does not stack with itself for ability, attack (misc) and damage bonuses | right (already there) | `CombatEffectSource.one_spell_groups_by_canonical_spell_id` |
| The same rule for **dodge** armour class bonuses and penalties (the only AC type that adds up) | **fixed**: a recast Force Armor/Aura/Shield stacked its dodge | `dodge_bonuses_add_up_across_sources_but_one_spell_counts_once`, `only_dodge_bonuses_stack_...`, `dodge_is_capped` |
| The same rule for **skill** increases and decreases | **fixed** | `skill_effects_follow_the_same_source_rule` |
| The same rule for **saving throw** effects | **fixed** (above) | |
| AC types other than dodge take the best value | right | `only_dodge_bonuses_stack_the_other_ac_types_take_the_best` |
| Linked effects apply together and end together; removing one removes the link | right | `linked_effects_apply_and_end_together`, `removing_one_member_of_a_link_...`, `EffectInstance.should_remove_every_member_of_a_shared_id_group` |
| A refused child (immunity) is not counted, and a link with every child refused is refused | right | `C08bTest.a_linked_child_the_target_is_immune_to_...`, `a_link_whose_every_child_is_refused_is_refused` |
| Effect icon state follows the effect | right | `EffectIconRules.set_effect_icon_links_the_icon_to_the_effect` |
| Death ends every effect except equipped, innate and the removefxondeath ones | right (existing function; one test checks only that effects do not survive a death) | `death_ends_effects_except_the_ones_that_outlive_it` |

How "source" is decided: an effect made by a script during a spell has that
spell's id and the caster (`Effect::setSaveFacingSpellId`); two such effects
from the same spell count once (the larger value), effects from different
spells, items or with no spell behind them add up. This is
`EffectModifierReducer`/`getEffectSourceKey`, which ability, attack-misc and
damage-increase modifiers already used.

## 4. Force power costs and Force-user modifiers

Already covered by `ForcePowerCost.*`, `CastSpellForcePoints.*`,
`TalentAffordability.*` and the max-force tests in `c03stats.cpp`; the audit
found nothing to change.

| rule | status |
|---|---|
| Cost = spells.2da forcepoints; the player character's cost is scaled by alignment (forceadjust.2da, goodcost / evilcost), a positive Charisma modifier trims a surcharge down to 1.0; truncated, at least 1; companions pay the base cost | right |
| Maximum force points: base + Wisdom modifier per spellcaster level (at least one per level); the player character counts only Force-class levels; droids and non-Force classes have none | right |
| Temporary force points are a separate pool; costs and drain come out of the whole value | right |
| An unaffordable power is not cast; casting from an item or by cheat costs nothing | right |

## Open questions (left unchanged)

1. **Natural 1 and natural 20 on a saving throw.** NWN makes a natural 1 an
   automatic failure and a natural 20 an automatic success. The engine does
   neither (`d20 + save >= DC`). Public KotOR documentation does not say; left
   as is.
2. **What a damage limit counts.** A resistance or reduction "with a limit"
   wears the limit down by the damage of each hit (the C08b specification),
   where the NWN documentation words it as the damage *absorbed*. The two
   differ only when a hit is larger than the resistance. Existing tests pin the
   current behaviour; kept.
3. **Conditioning feats.** One bonus (+1 Great Fortitude, +2 Iron Will, +3
   Lightning Reflexes, best only) is added to every save. The mapping is not in
   the public documentation consulted.
4. **Temporary hit points from several sources add up** (a separate pool per
   effect). d20 takes the larger. Left to the C08b design.
5. **Typed attack bonuses** (other than `Misc`) from effects are summed per
   effect without grouping by spell. Whether a recast stacks them is not
   documented.
