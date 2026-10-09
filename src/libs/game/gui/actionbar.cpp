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

#include "reone/game/coopsession.h"
#include "reone/game/gui/actionbar.h"

#include "reone/game/d20/forms.h"
#include "reone/game/d20/spells.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/trap.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/game/traps.h"
#include "reone/game/d20/spell.h"
#include "reone/game/game.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/input/event.h"

namespace reone {
namespace game {

static constexpr int kActionWidth = 26;

static void rotateActionBarArrow(std::shared_ptr<gui::Button> button) {
    if (!button) {
        return;
    }
    button->setBorderFillTransform(gui::Control::Border::FillTransform::Rotate180);
    button->setHilightFillTransform(gui::Control::Border::FillTransform::Rotate180);
}

void ActionBar::addDescription(std::shared_ptr<gui::Label> desc,
                               std::shared_ptr<gui::Label> background) {
    _desc = desc;
    _descBg = background;

    if (_desc) {
        _desc->setVisible(false);
    }
    if (_descBg) {
        _descBg->setVisible(false);
    }
}

void ActionBar::addSlot(std::shared_ptr<gui::Button> button,
                        std::shared_ptr<gui::Button> action,
                        std::shared_ptr<gui::Button> up,
                        std::shared_ptr<gui::Button> down) {
    rotateActionBarArrow(down);
    button->setSelectable(false);
    up->setSharpenBorderFillAlpha(true);
    down->setSharpenBorderFillAlpha(true);

    size_t slotIndex = _slots.size();
    _slots.push_back({
        ActionSlot(),
        button,
        action,
        up,
        down,
        up->border().fill,
        up->hilight().fill,
        down->border().fill,
        down->hilight().fill});
    up->setBorderFill(std::shared_ptr<graphics::Texture>());
    up->setHilightFill(std::shared_ptr<graphics::Texture>());
    down->setBorderFill(std::shared_ptr<graphics::Texture>());
    down->setHilightFill(std::shared_ptr<graphics::Texture>());

    auto cycleOnMouseWheel = [slotIndex, this](int x, int y) {
        ActionSlot &slot = _slots[slotIndex].slot;
        handleMouseWheel(slot, x, y);
    };
    action->setOnMouseWheel(cycleOnMouseWheel);
    up->setOnMouseWheel(cycleOnMouseWheel);
    down->setOnMouseWheel(cycleOnMouseWheel);

    action->setOnClick([slotIndex, this]() {
        ActionSlot &slot = _slots[slotIndex].slot;
        handleMouseButtonDown(slot);
    });
    up->setOnClick([slotIndex, this]() {
        ActionSlot &slot = _slots[slotIndex].slot;
        handleMouseWheel(slot, 0, 1);
    });
    down->setOnClick([slotIndex, this]() {
        ActionSlot &slot = _slots[slotIndex].slot;
        handleMouseWheel(slot, 0, -1);
    });

    auto handleSelectionChanged = [slotIndex, this](bool selected) {
        _slots[slotIndex].button->setSelected(selected);
    };
    action->setOnSelectionChanged(handleSelectionChanged);
    up->setOnSelectionChanged(handleSelectionChanged);
    down->setOnSelectionChanged(handleSelectionChanged);
}

void ActionBar::handleMouseWheel(ActionSlot &slot, int x, int y) {
    if (slot.actions.empty()) {
        return;
    }

    if (y > 0) {
        slot.indexSelected = slot.indexSelected == 0
                                 ? slot.actions.size() - 1
                                 : slot.indexSelected - 1;
    } else {
        slot.indexSelected = (slot.indexSelected + 1) % slot.actions.size();
    }
}

void ActionBar::activateSlot(int index) {
    if (index < 0 || index >= static_cast<int>(_slots.size())) {
        return;
    }
    handleMouseButtonDown(_slots[index].slot);
}

void ActionBar::handleMouseButtonDown(ActionSlot &slot) {
    if (slot.actions.empty()) {
        return;
    }

    std::shared_ptr<Creature> leader = _game.party().getLeader();
    if (!leader) {
        return;
    }

    ContextAction &ctxAction = slot.actions[slot.indexSelected];
    if (auto *coop = _game.coop(); coop && coop->isClient()) {
        // A co-op client asks the host: an item from the shared inventory,
        // or a power of its own creature. The host checks and carries it out.
        if (ctxAction.type == ActionType::SetTrap && ctxAction.item) {
            // Mines: laid a metre ahead, as below, by the host.
            float facing = leader->getFacing();
            glm::vec3 at = leader->position() + glm::vec3(-std::sin(facing), std::cos(facing), 0.0f);
            net::Command command;
            command.type = net::CommandType::SetMine;
            command.param = static_cast<int32_t>(coop->clientHostItemId(ctxAction.item->id()));
            command.point = {at.x, at.y, at.z};
            coop->submitCommand(command);
        } else if (ctxAction.type == ActionType::CastSpellAtObject && ctxAction.item) {
            coop->clientUseItem(ctxAction.item, 0);
        } else if (ctxAction.type == ActionType::CastSpellAtObject && ctxAction.spell) {
            coop->submitRequest(net::CommandType::UsePower, coop->controlledHostId(), static_cast<int32_t>(ctxAction.spell->type));
        }
        return;
    }
    if (ctxAction.type == ActionType::SetTrap) {
        // Mines are laid a metre ahead of the leader (design note c11_data).
        float facing = leader->getFacing();
        glm::vec3 ahead(-std::sin(facing), std::cos(facing), 0.0f);
        std::shared_ptr<Action> action = _game.newAction<SetTrapAction>(
            ctxAction.item, leader->position() + ahead);
        action->setUserAction(true);
        leader->addAction(std::move(action));
        return;
    }
    assert((ctxAction.type == ActionType::CastSpellAtObject) && "unexpected action");

    std::optional<std::shared_ptr<Item>> item = ctxAction.item ? std::optional(ctxAction.item) : std::nullopt;

    std::shared_ptr<Action> action = _game.newAction<CastSpellAtObjectAction>(
        ctxAction.spell, leader, item);
    action->setUserAction(true);
    leader->addAction(std::move(action));
}

static std::string getDescription(const ContextAction &action, const Creature &creature) {
    if (action.type != ActionType::CastSpellAtObject && action.type != ActionType::SetTrap) {
        return "";
    }

    if (action.item) {
        const std::string &name = action.item->localizedName();
        int stackSize = action.item->stackSize();
        if (stackSize == 1) {
            return name;
        }
        return str(boost::format("%s (%d)") % name % stackSize);
    }

    // A K2 Force form: marked while it is the active form.
    if (action.spell && action.spell->userType == forms::kFormUserType &&
        creature.activeForm() == static_cast<int>(action.spell->type)) {
        return action.spell->name + " *";
    }
    return action.spell->name;
}

void ActionBar::update() {
    std::shared_ptr<Creature> leader = _game.party().getLeader();
    if (!leader) {
        return;
    }
    // A co-op client's companion uses the shared party inventory, which the
    // host's player character holds: its usable items are the ones listed.
    const ItemAttributes *sharedAttrs = nullptr;
    if (auto *coop = _game.coop(); coop && coop->isClient()) {
        if (auto holder = std::dynamic_pointer_cast<Creature>(_game.party().sharedInventoryReceiver(leader))) {
            sharedAttrs = &holder->itemAttributes();
        }
    }
    // Otherwise a companion the player controls has its own items and the
    // party's: K1 has one party inventory, held by the player character.
    auto usable = [&](ItemAttributes::Kind kind) {
        return sharedAttrs ? sharedAttrs->spells(kind) : _game.party().usableItemSpells(*leader, kind);
    };

    for (size_t i = 0; i < _slots.size(); ++i) {
        Slot &guiSlot = _slots[i];
        ActionSlot &slot = guiSlot.slot;
        slot.actions.clear();
        switch (i) {
        case 0: {
            // TODO: Force powers.
            // K2 Force forms (the K2 systems merge): the forms the leader
            // knows; using one makes it the active form (design note k2_merge).
            if (_services.resource.k2Merge) {
                for (int form = forms::kShiiCho; form <= forms::kMastery; ++form) {
                    auto type = static_cast<SpellType>(form);
                    if (!leader->attributes().hasSpell(type)) {
                        continue;
                    }
                    if (auto spell = _services.game.spells.get(type)) {
                        slot.actions.push_back(ContextAction(nullptr, spell));
                    }
                }
            }
            break;
        }
        case 1: {
            // Healing items: medpacks, stimulants.
            for (const auto &[item, spell] : usable(ItemAttributes::Kind::Healing)) {
                slot.actions.push_back(ContextAction(item, spell));
            }

            break;
        }
        case 2: {
            // Defensive items: shields.
            for (const auto &[item, spell] : usable(ItemAttributes::Kind::Defensive)) {
                slot.actions.push_back(ContextAction(item, spell));
            }
            break;
        }
        case 3: {
            // Mines: items whose first property is a trap (traps.2da row).
            auto holder = _game.party().sharedInventoryReceiver(leader);
            for (const auto &item : (holder ? *holder : static_cast<Object &>(*leader)).items()) {
                if (item && mineTrapRow(*item) >= 0) {
                    ContextAction action(ActionType::SetTrap);
                    action.item = item;
                    slot.actions.push_back(std::move(action));
                }
            }
            break;
        }
        default:
            // TODO: TSL slots.
            break;
        }

        if (!slot.actions.empty()) {
            slot.indexSelected = std::min(slot.indexSelected, slot.actions.size() - 1);
        } else {
            slot.indexSelected = 0;
        }
        bool showArrows = slot.actions.size() > 1;
        guiSlot.up->setBorderFill(showArrows ? guiSlot.upBorderFill : nullptr);
        guiSlot.up->setHilightFill(showArrows ? guiSlot.upHilightFill : nullptr);
        guiSlot.down->setBorderFill(showArrows ? guiSlot.downBorderFill : nullptr);
        guiSlot.down->setHilightFill(showArrows ? guiSlot.downHilightFill : nullptr);
    }

    bool showDesc = false;
    for (Slot &guiSlot : _slots) {
        ActionSlot &slot = guiSlot.slot;

        if (slot.actions.empty() || !guiSlot.button->isSelected()) {
            continue;
        }

        showDesc = true;
        const ContextAction &selectedAction = slot.actions[slot.indexSelected];
        if (_desc) {
            _desc->setTextMessage(getDescription(selectedAction, *leader));
        }
    }

    if (_desc) {
        _desc->setVisible(showDesc);
    }
    if (_descBg) {
        _descBg->setVisible(showDesc);
    }
}

void ActionBar::render(float layoutScale) {
    for (const Slot &slot : _slots) {
        const std::vector<ContextAction> &actions = slot.slot.actions;
        if (actions.empty()) {
            continue;
        }

        gui::Control::Extent slotExtent = slot.button->extent();
        // Icons are artwork in the button's layout space, not text.
        float iconSize = kActionWidth * layoutScale;
        int centerX, centerY;
        slotExtent.getCenter(centerX, centerY);
        float x = centerX - iconSize / 2;
        float y = centerY - iconSize / 2;

        glm::mat4 transform(1.0f);
        transform = glm::translate(transform, glm::vec3(x, y, 0.0f));
        transform = glm::scale(transform, glm::vec3(iconSize, iconSize, 1.0f));
        renderContextActionIcon(actions[slot.slot.indexSelected], transform, _services,
                                _game.hudPresentation().resource.textures);
    }
}

} // namespace game
} // namespace reone
