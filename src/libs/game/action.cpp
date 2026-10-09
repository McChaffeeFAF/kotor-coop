/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include "reone/game/action.h"

#include <algorithm>

#include "reone/game/action/usefeat.h"
#include "reone/game/attack.h"
#include "reone/game/object.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

void Action::requireRuntimeObject(const std::shared_ptr<Object> &object) {
    if (object) {
        _runtimeDependencies.emplace_back(object);
    }
}

bool Action::runtimeDependenciesLive() const {
    return std::all_of(
        _runtimeDependencies.begin(),
        _runtimeDependencies.end(),
        [](const auto &reference) { return reference.resolve() != nullptr; });
}

void Action::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    warn("Action execution not implemented: " + std::to_string(static_cast<int>(_type)));
    complete();
}

bool isHostileAction(Action &action) {
    switch (action.type()) {
    case ActionType::AttackObject:
        return true;
    case ActionType::UseFeat:
        return isPhysicalAttackFeat(static_cast<UseFeatAction &>(action).feat());
    default:
        break;
    }
    return false;
}

std::string_view actionTypeName(ActionType type) {
    switch (type) {
    case ActionType::MoveToPoint:
        return "move_to_point";
    case ActionType::PickUpItem:
        return "pick_up_item";
    case ActionType::DropItem:
        return "drop_item";
    case ActionType::AttackObject:
        return "attack_object";
    case ActionType::CastSpell:
        return "cast_spell";
    case ActionType::OpenDoor:
        return "open_door";
    case ActionType::CloseDoor:
        return "close_door";
    case ActionType::DialogObject:
        return "dialog_object";
    case ActionType::DisableTrap:
        return "disable_trap";
    case ActionType::RecoverTrap:
        return "recover_trap";
    case ActionType::FlagTrap:
        return "flag_trap";
    case ActionType::ExamineTrap:
        return "examine_trap";
    case ActionType::SetTrap:
        return "set_trap";
    case ActionType::OpenLock:
        return "open_lock";
    case ActionType::Lock:
        return "lock";
    case ActionType::UseObject:
        return "use_object";
    case ActionType::AnimalEmpathy:
        return "animal_empathy";
    case ActionType::Rest:
        return "rest";
    case ActionType::Taunt:
        return "taunt";
    case ActionType::ItemCastSpell:
        return "item_cast_spell";
    case ActionType::CounterSpell:
        return "counter_spell";
    case ActionType::Heal:
        return "heal";
    case ActionType::PickPocket:
        return "pick_pocket";
    case ActionType::Follow:
        return "follow";
    case ActionType::Wait:
        return "wait";
    case ActionType::Sit:
        return "sit";
    case ActionType::FollowLeader:
        return "follow_leader";
    case ActionType::FollowOwner:
        return "follow_owner";
    case ActionType::DoCommand:
        return "do_command";
    case ActionType::StartConversation:
        return "start_conversation";
    case ActionType::PauseConversation:
        return "pause_conversation";
    case ActionType::ResumeConversation:
        return "resume_conversation";
    case ActionType::MoveToObject:
        return "move_to_object";
    case ActionType::OpenContainer:
        return "open_container";
    case ActionType::JumpToObject:
        return "jump_to_object";
    case ActionType::JumpToLocation:
        return "jump_to_location";
    case ActionType::RandomWalk:
        return "random_walk";
    case ActionType::MoveToLocation:
        return "move_to_location";
    case ActionType::MoveAwayFromObject:
        return "move_away_from_object";
    case ActionType::EquipItem:
        return "equip_item";
    case ActionType::UnequipItem:
        return "unequip_item";
    case ActionType::SpeakString:
        return "speak_string";
    case ActionType::PlayAnimation:
        return "play_animation";
    case ActionType::CastSpellAtObject:
        return "cast_spell_at_object";
    case ActionType::GiveItem:
        return "give_item";
    case ActionType::TakeItem:
        return "take_item";
    case ActionType::ForceFollowObject:
        return "force_follow_object";
    case ActionType::CastSpellAtLocation:
        return "cast_spell_at_location";
    case ActionType::SpeakStringByStrRef:
        return "speak_string_by_str_ref";
    case ActionType::UseFeat:
        return "use_feat";
    case ActionType::UseSkill:
        return "use_skill";
    case ActionType::UseTalentOnObject:
        return "use_talent_on_object";
    case ActionType::UseTalentAtLocation:
        return "use_talent_at_location";
    case ActionType::InteractObject:
        return "interact_object";
    case ActionType::MoveAwayFromLocation:
        return "move_away_from_location";
    case ActionType::SurrenderToEnemies:
        return "surrender_to_enemies";
    case ActionType::EquipMostDamagingMelee:
        return "equip_most_damaging_melee";
    case ActionType::EquipMostDamagingRanged:
        return "equip_most_damaging_ranged";
    case ActionType::EquipMostEffectiveArmor:
        return "equip_most_effective_armor";
    case ActionType::CastFakeSpellAtObject:
        return "cast_fake_spell_at_object";
    case ActionType::CastFakeSpellAtLocation:
        return "cast_fake_spell_at_location";
    case ActionType::BarkString:
        return "bark_string";
    case ActionType::SwitchWeapons:
        return "switch_weapons";
    case ActionType::PutDownItem:
        return "put_down_item";
    case ActionType::CutsceneAttack:
        return "cutscene_attack";
    case ActionType::Invalid:
        return "invalid";
    case ActionType::QueueEmpty:
        return "queue_empty";
    }
    return {};
}

} // namespace game

} // namespace reone
