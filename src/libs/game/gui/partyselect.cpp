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

#include "reone/game/gui/partyselect.h"

#include <utility>

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/party.h"
#include "reone/game/portrait.h"
#include "reone/game/portraits.h"
#include "reone/game/script/runner.h"
#include "reone/game/d20/classes.h"
#include "reone/game/types.h"
#include "reone/gui/sceneinitializer.h"
#include "reone/resource/provider/models.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/script/types.h"
#include "reone/system/logutil.h"


using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

namespace game {

static constexpr int kMaxFollowerCount = 2;

static int g_strRefAdd = 38455;
static int g_strRefRemove = 38456;

static glm::vec3 g_kotorColorOn = {0.984314f, 1.0f, 0};
// Neither game authors an in-party colour; K1's green is the retail runtime
// tint. TSL authors white for its buttons' selected state, and the K1 green
// clashes with its teal-and-sand palette.
static glm::vec3 g_kotorColorAdded = {0, 0.831373f, 0.090196f};
static glm::vec3 g_tslColorAdded = {1.0f, 1.0f, 1.0f};

static constexpr int kStrRefAvailableSlots = 1564;
static constexpr int kStrRefLevel = 32154;

// K1 companions on K2's ring (design note p3_menus): each takes the seat K2 gives its
// nearest counterpart. G0-T0's seat and the two seats K2 shares
// (Hanharr, the Disciple) stay empty.
static constexpr int kK1NpcSeatInK2Ring[kKotorNpcCount] {
    4, // Bastila: the Handmaiden's seat
    2, // Canderous: Mandalore's (the same man)
    0, // Carth: Atton's
    5, // HK-47: HK-47's
    6, // Jolee: Kreia's
    9, // Juhani: Visas's
    7, // Mission: Mira's
    8, // T3-M4: T3-M4's
    1, // Zaalbar: Bao-Dur's
};

PartySelection::PartySelection(Game &game, ServicesView &services, bool hosted) :
    GameGUI(game, services),
    _hosted(hosted) {

    if (isTSL()) {
        _resRef = "partyselect_p";
    } else {
        _resRef = "partyselection";
    }
}

void PartySelection::onGUILoaded() {
    loadBackground(BackgroundType::Menu);

    bindControls();

    if (isTSL()) {
        // LBL_BAR1 and LBL_BAR2 are not a section-title strip here: they are
        // authored twenty pixels apart with the available-slot count centred
        // between them. Collapsing them the way the in-game menus do strands
        // the count below the strip, so the bars keep their authored places
        // and only take the shell tint.
        tintK2InGameHeader();
        for (auto &control : {
                 _controls.LBL_NA0, _controls.LBL_NA1, _controls.LBL_NA2, _controls.LBL_NA3,
                 _controls.LBL_NA4, _controls.LBL_NA5, _controls.LBL_NA6, _controls.LBL_NA7,
                 _controls.LBL_NA8, _controls.LBL_NA9, _controls.LBL_NA10, _controls.LBL_NA11}) {
            control->setTintBorderFill(true);
        }
        // Inside the in-game menu the host draws the title; the stand-alone
        // screen (ShowPartySelectionGUI) keeps its own.
        if (_hosted) {
            useK2ShellTitle(_controls.LBL_TITLE);
        }
        for (auto &button : {_controls.BTN_ACCEPT, _controls.BTN_DONE, _controls.BTN_BACK}) {
            enableK2ButtonBodyFill(button);
        }
    }

    for (int i = 0; i < npcCount(); ++i) {
        ToggleButton &button = getNpcButton(i);
        button.setOnColor(g_kotorColorOn);
        button.setBorderColorOverride(isTSL() ? g_tslColorAdded : g_kotorColorAdded);
        button.setUseBorderColorOverride(false);
    }

    bindEventHandlers();
}

void PartySelection::bindEventHandlers() {
    _controls.BTN_ACCEPT->setOnClick([this]() {
        onAcceptButtonClick();
    });
    _controls.BTN_DONE->setOnClick([this]() {
        onDoneButtonClick();
    });
    _controls.BTN_BACK->setOnClick([this]() {
        _game.openInGame();
        if (!_context.exitScript.empty()) {
            _game.scriptRunner().run(_context.exitScript);
        }
    });
    for (int slot = 0; slot < slotCount(); ++slot) {
        if (auto button = slotButton(slot)) {
            button->setOnClick([this, slot]() {
                int npc = npcOfSlot(slot);
                if (npc >= 0) {
                    onNpcButtonClick(npc);
                }
            });
        }
    }
}

void PartySelection::prepare(const PartySelectionContext &ctx) {
    _gui->setEventListener(*this);
    bindEventHandlers();
    if (isTSL() && _controls.LBL_TITLE) {
        // The hosted and stand-alone screens share one GUI instance: each
        // sets the title its own way when it opens.
        _controls.LBL_TITLE->setVisible(!_hosted);
    }

    _context = ctx;
    _availableCount = kMaxFollowerCount;
    _selectedNpc = -1;

    for (int i = 0; i < kMaxNpcCount; ++i) {
        _added[i] = false;
    }
    for (int i = 0; i < npcCount(); ++i) {
        getNpcButton(i).setUseBorderColorOverride(false);
    }
    refreshNpcButtons();

    Party &party = _game.party();
    for (auto &member : party.members()) {
        if (member.npc != kNpcPlayer) {
            addNpc(member.npc);
        }
    }
    if (ctx.forceNpc1 >= 0) {
        addNpc(ctx.forceNpc1);
    }
    if (ctx.forceNpc2 >= 0) {
        addNpc(ctx.forceNpc2);
    }
    if (isK2LayoutInK1()) {
        // K1's roster is nine NPCs: the seats no K1 companion takes stay empty.
        for (int slot = 0; slot < slotCount(); ++slot) {
            if (npcOfSlot(slot) >= 0) {
                continue;
            }
            for (Control *control : std::initializer_list<Control *> {slotButton(slot), slotPortrait(slot), slotUnavailable(slot)}) {
                if (control) {
                    control->setVisible(false);
                }
            }
        }
    }

    for (int i = 0; i < npcCount(); ++i) {
        ToggleButton &BTN_NPC = getNpcButton(i);
        Label &LBL_CHAR = *slotPortrait(slotOf(i));
        Label &LBL_NA = *slotUnavailable(slotOf(i));

        if (auto member = party.getAvailableMember(i, true)) {
            auto portrait = member->portrait();
            BTN_NPC.setDisabled(false);
            BTN_NPC.setVisible(true);
            LBL_CHAR.setBorderFill(std::move(portrait));
            LBL_NA.setVisible(false);
        } else {
            BTN_NPC.setDisabled(true);
            BTN_NPC.setVisible(true);
            LBL_CHAR.setBorderFill(std::shared_ptr<Texture>(nullptr));
            LBL_NA.setVisible(true);
        }
    }
    if (_game.isTSL()) {
        // Handmaiden/Disciple and Mira/Hanharr are authored on one shared
        // slot each. The not-available overlay belongs to the slot, not to
        // each NPC: with one of the pair available, the other's overlay and
        // disabled button frame would draw over the occupant's portrait, and
        // with both away a single overlay is enough.
        static constexpr std::pair<int, int> kSharedSlots[] {
            {4, 11}, // Handmaiden, Disciple
            {7, 10}, // Mira, Hanharr
        };
        for (const auto &slot : kSharedSlots) {
            bool firstAvailable = party.isMemberAvailable(slot.first);
            bool secondAvailable = party.isMemberAvailable(slot.second);
            slotUnavailable(slot.first)->setVisible(!firstAvailable && !secondAvailable);
            slotUnavailable(slot.second)->setVisible(false);
            getNpcButton(slot.first).setVisible(firstAvailable || !secondAvailable);
            getNpcButton(slot.second).setVisible(secondAvailable);
        }
    }
    refreshAvailableCount();

    if (isTSL()) {
        // K2 opens on the first available member, in ring order, with its
        // model and name shown; with nobody available the panel is empty.
        for (int slot = 0; slot < slotCount(); ++slot) {
            int npc = npcOfSlot(slot);
            if (npc >= 0 && party.isMemberAvailable(npc)) {
                _selectedNpc = npc;
                break;
            }
        }
        refreshNpcButtons();
        if (_selectedNpc >= 0) {
            refreshAcceptButton();
        }
        refreshPreview();
    }
}

int PartySelection::slotCount() const {
    return isTSL() ? kTSLNpcCount : kKotorNpcCount;
}

int PartySelection::slotOf(int npc) const {
    if (isK2LayoutInK1() && npc >= 0 && npc < kKotorNpcCount) {
        return kK1NpcSeatInK2Ring[npc];
    }
    return npc;
}

int PartySelection::npcOfSlot(int slot) const {
    for (int npc = 0; npc < npcCount(); ++npc) {
        if (slotOf(npc) == slot) {
            return npc;
        }
    }
    return -1;
}

ToggleButton *PartySelection::slotButton(int slot) const {
    const std::shared_ptr<ToggleButton> *buttons[] {
        &_controls.BTN_NPC0, &_controls.BTN_NPC1, &_controls.BTN_NPC2, &_controls.BTN_NPC3,
        &_controls.BTN_NPC4, &_controls.BTN_NPC5, &_controls.BTN_NPC6, &_controls.BTN_NPC7,
        &_controls.BTN_NPC8, &_controls.BTN_NPC9, &_controls.BTN_NPC10, &_controls.BTN_NPC11};
    return slot >= 0 && slot < kMaxNpcCount ? buttons[slot]->get() : nullptr;
}

Label *PartySelection::slotPortrait(int slot) const {
    const std::shared_ptr<Label> *labels[] {
        &_controls.LBL_CHAR0, &_controls.LBL_CHAR1, &_controls.LBL_CHAR2, &_controls.LBL_CHAR3,
        &_controls.LBL_CHAR4, &_controls.LBL_CHAR5, &_controls.LBL_CHAR6, &_controls.LBL_CHAR7,
        &_controls.LBL_CHAR8, &_controls.LBL_CHAR9, &_controls.LBL_CHAR10, &_controls.LBL_CHAR11};
    return slot >= 0 && slot < kMaxNpcCount ? labels[slot]->get() : nullptr;
}

Label *PartySelection::slotUnavailable(int slot) const {
    const std::shared_ptr<Label> *labels[] {
        &_controls.LBL_NA0, &_controls.LBL_NA1, &_controls.LBL_NA2, &_controls.LBL_NA3,
        &_controls.LBL_NA4, &_controls.LBL_NA5, &_controls.LBL_NA6, &_controls.LBL_NA7,
        &_controls.LBL_NA8, &_controls.LBL_NA9, &_controls.LBL_NA10, &_controls.LBL_NA11};
    return slot >= 0 && slot < kMaxNpcCount ? labels[slot]->get() : nullptr;
}

void PartySelection::refreshPreview() {
    if (!isTSL()) {
        return;
    }
    std::shared_ptr<Creature> member;
    if (_selectedNpc >= 0 && _game.party().isMemberAvailable(_selectedNpc)) {
        member = _game.party().getAvailableMember(_selectedNpc, true);
    }
    for (auto &control : std::initializer_list<std::shared_ptr<Label>> {_controls.LBL_NPC_NAME, _controls.LBL_NPC_LEVEL, _controls.LBL_NAMEBACK}) {
        if (control) {
            control->setVisible(member != nullptr);
        }
    }
    if (!member) {
        if (_controls.LBL_3D) {
            _controls.LBL_3D->setSceneName("");
        }
        return;
    }
    if (_controls.LBL_NPC_NAME) {
        _controls.LBL_NPC_NAME->setTextMessage(member->name());
    }
    if (_controls.LBL_NPC_LEVEL) {
        // "Level <n> <class>": K1 content, so K1's TLK and classes.
        auto &attributes = member->attributes();
        std::string className;
        for (int position = 2; position >= 1 && className.empty(); --position) {
            auto clazz = attributes.getClassByPosition(position);
            if (clazz != ClassType::Invalid) {
                className = _services.game.classes.get(clazz)->name();
            }
        }
        std::string text = _services.resource.strings.getText(kStrRefLevel) + " " +
                           std::to_string(attributes.getAggregateLevel());
        if (!className.empty()) {
            text += " " + className;
        }
        _controls.LBL_NPC_LEVEL->setTextMessage(text);
    }
    if (_controls.LBL_3D) {
        auto &sceneGraph = _services.scene.graphs.get(kScenePartySelect);
        const auto &extent = _controls.LBL_3D->extent();
        float aspect = extent.height > 0 ? extent.width / static_cast<float>(extent.height) : 1.0f;
        SceneInitializer(sceneGraph)
            .aspect(aspect)
            .depth(kDefaultClipPlaneNear, 10.0f)
            .modelSupplier(std::bind(&PartySelection::getPreviewModel, this, std::placeholders::_1))
            .modelOffset(glm::vec2(0.0f, 1.7f))
            .cameraFromModelNode("camerahook")
            .invoke();
        _controls.LBL_3D->setSceneName(kScenePartySelect);
    }
}

std::shared_ptr<ModelSceneNode> PartySelection::getPreviewModel(ISceneGraph &sceneGraph) const {
    auto member = _game.party().getAvailableMember(_selectedNpc, true);
    auto light = _services.resource.models.get("charrec_light");
    if (!member || !light) {
        return nullptr;
    }
    auto character = _game.newPresentationCreature(sceneGraph.name());
    character->setFacing(-glm::half_pi<float>());
    character->setAppearance(member->appearance());
    for (auto &item : member->equipment()) {
        if (item.first == InventorySlots::body) {
            auto previewItem = _game.newPresentationItem();
            previewItem->clone(*item.second);
            character->equip(item.first, previewItem);
        }
    }
    character->loadAppearance();
    character->updateModelAnimation();

    auto sceneModel = sceneGraph.newModel(*light, ModelUsage::GUI);
    sceneModel->attach("charrec_light", *character->sceneNode());
    return sceneModel;
}

void PartySelection::addNpc(int npc) {
    if (!isSupportedNpc(npc) || _added[npc]) {
        return;
    }

    --_availableCount;
    _added[npc] = true;
    getNpcButton(npc).setUseBorderColorOverride(true);
    refreshAvailableCount();
}

ToggleButton &PartySelection::getNpcButton(int npc) {
    return *slotButton(slotOf(npc));
}

bool PartySelection::isSupportedNpc(int npc) const {
    return npc >= 0 && npc < npcCount();
}

int PartySelection::npcCount() const {
    return _game.isTSL() ? kTSLNpcCount : kKotorNpcCount;
}

void PartySelection::onDoneButtonClick() {
    std::vector<int> selectedNpcs;
    for (int npc = 0; npc < npcCount(); ++npc) {
        if (_added[npc]) {
            selectedNpcs.push_back(npc);
        }
    }
    _game.reconcilePartySelection(selectedNpcs);
    _game.openInGame();
    if (!_context.exitScript.empty()) {
        _game.scriptRunner().run(_context.exitScript);
    }
}

void PartySelection::onAcceptButtonClick() {
    if (_selectedNpc == -1)
        return;

    bool added = _added[_selectedNpc];
    if (added && _context.forceNpc1 != _selectedNpc && _context.forceNpc2 != _selectedNpc) {
        removeNpc(_selectedNpc);
        refreshAcceptButton();

    } else if (!added && _availableCount > 0) {
        addNpc(_selectedNpc);
        refreshAcceptButton();
    }
}

void PartySelection::refreshAvailableCount() {
    // K2's layout has no Available Slots label of its own: the count reads
    // "Available Slots <n>". K1's label sits beside a bare number.
    std::string count = std::to_string(_availableCount);
    if (isTSL() && !_controls.LBL_AVAILABLE) {
        std::string label = _services.resource.strings.getText(kStrRefAvailableSlots);
        if (!label.empty()) {
            count = label + " " + count;
        }
    }
    _controls.LBL_COUNT->setTextMessage(count);
}

void PartySelection::refreshAcceptButton() {
    std::string text(_services.resource.strings.getText(_added[_selectedNpc] ? g_strRefRemove : g_strRefAdd));
    _controls.BTN_ACCEPT->setTextMessage(text);
}

void PartySelection::removeNpc(int npc) {
    ++_availableCount;
    _added[npc] = false;
    getNpcButton(npc).setUseBorderColorOverride(false);
    refreshAvailableCount();
}

void PartySelection::onNpcButtonClick(int npc) {
    if (!isSupportedNpc(npc)) {
        return;
    }

    _selectedNpc = npc;
    refreshNpcButtons();
    refreshAcceptButton();
    refreshPreview();
}

void PartySelection::refreshNpcButtons() {
    for (int i = 0; i < npcCount(); ++i) {
        ToggleButton &button = getNpcButton(i);

        if ((i == _selectedNpc && !button.isOn()) ||
            (i != _selectedNpc && button.isOn())) {

            button.toggle();
        }
    }
}

} // namespace game

} // namespace reone
