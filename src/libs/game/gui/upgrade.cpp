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

#include "reone/game/gui/upgrade.h"

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include "reone/graphics/texture.h"
#include "reone/gui/guis.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/strings.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/itemdescription.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

// TLK: slot names on a lightsaber.
static constexpr int kPowerCrystalStrRef = 36977;
static constexpr int kColorCrystalStrRef = 36978;

static bool isEscape(const input::Event &event) {
    return event.type == input::EventType::KeyDown && !event.key.repeat &&
           event.key.code == input::KeyCode::Escape;
}

static void configureItemList(ListBox &list, const glm::vec3 &base, const glm::vec3 &hilight) {
    list.setSelectionMode(ListBox::SelectionMode::OnClick);
    list.setRenderItemIconsForButtonProto(true);
    if (auto proto = list.protoItemOrNull()) {
        Control::Text text(proto->text());
        text.align = Control::TextAlign::LeftCenter;
        proto->setText(text);
        proto->setHilightColor(hilight);
    }
}

// --- UpgradeItemsPanel -----------------------------------------------------

void UpgradeItemsPanel::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    centerRootInCanvas(isTSL() ? 800 : 640, isTSL() ? 600 : 480);
    _list = findControl<ListBox>("LB_ITEMS");
    _description = findControl<ListBox>("LB_DESCRIPTION");
    _upgradeButton = findControl<Button>("BTN_UPGRADEITEM");
    _backButton = findControl<Button>("BTN_BACK");
    if (_list) {
        configureItemList(*_list, _baseColor, _hilightColor);
        _list->setOnItemClick([this](const std::string &) {
            describeSelection();
        });
        _list->setOnItemDoubleClick([this](const std::string &) {
            activateSelection();
        });
    }
    if (_description) {
        _description->setProtoMatchContent(true);
    }
    if (_upgradeButton) {
        _upgradeButton->setOnClick([this]() {
            activateSelection();
        });
    }
    if (_backButton) {
        _backButton->setOnClick([this]() {
            _host.back();
        });
    }
}

void UpgradeItemsPanel::show(UpgradeCategory category) {
    _category = category;
    refresh();
}

void UpgradeItemsPanel::refresh() {
    _items = _host.bench().partyItems(_category);
    if (!_list) {
        return;
    }
    _list->clearItems();
    for (const auto &item : _items) {
        ListBox::Item row;
        row.tag = std::to_string(item->id());
        row.text = item->localizedName();
        row.iconTexture = item->icon();
        row.iconFrame = itemFrameTexture(item->stackSize());
        _list->addItem(std::move(row));
    }
    // The K2 workbench's crafting (K2 systems merge): K1's items page has no
    // spare button, so two rows at the end of its list open it.
    _craftingRows = _host.craftingAvailable();
    if (_craftingRows) {
        for (const char *text : {"[Create items]", "[Break down items]"}) {
            ListBox::Item row;
            row.tag = text;
            row.text = text;
            _list->addItem(std::move(row));
        }
    }
    bool empty = _items.empty() && !_craftingRows;
    _list->setSelectedItemIndex(empty ? -1 : 0);
    if (_upgradeButton) {
        _upgradeButton->setDisabled(empty);
    }
    describeSelection();
}

int UpgradeItemsPanel::selectedCraftingRow() const {
    int index = _list ? _list->selectedItemIndex() : -1;
    int row = index - static_cast<int>(_items.size());
    return _craftingRows && row >= 0 && row < 2 ? row : -1;
}

void UpgradeItemsPanel::activateSelection() {
    int crafting = selectedCraftingRow();
    if (crafting >= 0) {
        _host.openCrafting(crafting == 0 ? CraftingPanel::Mode::Create : CraftingPanel::Mode::BreakDown);
        return;
    }
    if (auto item = selectedItem()) {
        _host.openItem(item);
    }
}

std::shared_ptr<Item> UpgradeItemsPanel::selectedItem() const {
    int index = _list ? _list->selectedItemIndex() : -1;
    if (index < 0 || index >= static_cast<int>(_items.size())) {
        return nullptr;
    }
    return _items[index];
}

void UpgradeItemsPanel::describeSelection() {
    if (!_description) {
        return;
    }
    _description->clearItems();
    if (int crafting = selectedCraftingRow(); crafting >= 0) {
        _description->addTextLinesAsItems(crafting == 0
                                              ? "Make items from components and chemicals."
                                              : "Break items down into components and chemicals.");
        return;
    }
    if (auto item = selectedItem()) {
        _description->addTextLinesAsItems(joinItemDescriptionLines(buildItemDescriptionLines(*item, _services)));
    }
}

// --- CraftingPanel ---------------------------------------------------------

static const char *materialName(crafting::Material material) {
    return material == crafting::Material::Chemicals ? "chemicals" : "components";
}

static std::string skillName(ServicesView &services, SkillType skill) {
    auto skills = services.resource.twoDas.get("skills");
    if (!skills || static_cast<int>(skill) < 0 || static_cast<int>(skill) >= skills->getRowCount()) {
        return "";
    }
    return services.resource.strings.getText(skills->getInt(static_cast<int>(skill), "name", -1));
}

void CraftingPanel::init() {
    _gui = _presentation.guis.getPrivateCopy(_resRef, [this](IGUI &gui) { preload(gui); });
    if (!_gui) {
        throw std::runtime_error("GUI not found: " + _resRef);
    }
    _gui->setEventListener(*this);
    onGUILoaded();
}

void CraftingPanel::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    centerRootInCanvas(isTSL() ? 800 : 640, isTSL() ? 600 : 480);
    _list = findControl<ListBox>("LB_ITEMS");
    _description = findControl<ListBox>("LB_DESCRIPTION");
    _actionButton = findControl<Button>("BTN_UPGRADEITEM");
    _backButton = findControl<Button>("BTN_BACK");
    if (_list) {
        configureItemList(*_list, _baseColor, _hilightColor);
        _list->setOnItemClick([this](const std::string &) {
            describeSelection();
        });
        _list->setOnItemDoubleClick([this](const std::string &) {
            activate(_list->selectedItemIndex());
        });
    }
    if (_description) {
        _description->setProtoMatchContent(true);
    }
    if (_actionButton) {
        _actionButton->setOnClick([this]() {
            if (_list) {
                activate(_list->selectedItemIndex());
            }
        });
    }
    if (_backButton) {
        _backButton->setOnClick([this]() {
            _host.back();
        });
    }
}

std::shared_ptr<Creature> CraftingPanel::crafter() const {
    return _game.party().getLeader();
}

void CraftingPanel::show(Mode mode) {
    _mode = mode;
    refresh();
}

void CraftingPanel::refresh() {
    auto leader = crafter();
    _recipes.clear();
    _breakable.clear();
    if (leader) {
        if (_mode == Mode::Create) {
            _recipes = _crafting.recipes(*leader);
        } else {
            _breakable = _crafting.breakdownCandidates();
        }
    }
    if (_actionButton) {
        _actionButton->setTextMessage(_mode == Mode::Create ? "Create Item" : "Break Down");
    }
    if (!_list) {
        return;
    }
    _list->clearItems();
    {
        ListBox::Item row;
        row.tag = "mode";
        row.text = _mode == Mode::Create ? "[Break down items]" : "[Create items]";
        _list->addItem(std::move(row));
    }
    int rows = 1;
    if (_mode == Mode::Create) {
        for (size_t i = 0; i < _recipes.size(); ++i) {
            const auto &entry = _recipes[i];
            ListBox::Item row;
            row.tag = std::to_string(i);
            row.text = str(boost::format("%s%s (%d %s)") %
                           (entry.skilled && entry.affordable ? "" : "- ") % entry.name % entry.cost %
                           materialName(entry.recipe.material));
            _list->addItem(std::move(row));
            ++rows;
        }
    } else if (leader) {
        for (const auto &item : _breakable) {
            ListBox::Item row;
            row.tag = std::to_string(item->id());
            row.text = str(boost::format("%s (+%d %s)") % item->localizedName() %
                           _crafting.breakdownYieldFor(*item, *leader) %
                           materialName(crafting::materialOfBaseItem(item->baseItemType())));
            row.iconTexture = item->icon();
            row.iconFrame = itemFrameTexture(item->stackSize());
            _list->addItem(std::move(row));
            ++rows;
        }
    }
    _list->setSelectedItemIndex(rows > 1 ? 1 : 0);
    describeSelection();
}

bool CraftingPanel::activate(int row) {
    auto leader = crafter();
    if (row == 0) {
        show(_mode == Mode::Create ? Mode::BreakDown : Mode::Create);
        return true;
    }
    if (!leader || row < 1) {
        return false;
    }
    bool done = false;
    size_t index = static_cast<size_t>(row - 1);
    if (_mode == Mode::Create && index < _recipes.size()) {
        done = _crafting.create(_recipes[index].recipe, *leader);
    } else if (_mode == Mode::BreakDown && index < _breakable.size()) {
        done = _crafting.breakDown(_breakable[index], *leader);
    }
    int rows = 1 + static_cast<int>(_mode == Mode::Create ? _recipes.size() : _breakable.size());
    refresh();
    rows = 1 + static_cast<int>(_mode == Mode::Create ? _recipes.size() : _breakable.size());
    if (_list && row < rows) {
        _list->setSelectedItemIndex(row);
        describeSelection();
    }
    return done;
}

void CraftingPanel::describeSelection() {
    if (!_description || !_list) {
        return;
    }
    _description->clearItems();
    auto leader = crafter();
    std::string totals = str(boost::format("Components: %d\nChemicals: %d") %
                             _crafting.components() % _crafting.chemicals());
    int row = _list->selectedItemIndex();
    if (row <= 0 || !leader) {
        _description->addTextLinesAsItems(totals);
        return;
    }
    size_t index = static_cast<size_t>(row - 1);
    std::string text;
    if (_mode == Mode::Create && index < _recipes.size()) {
        const auto &entry = _recipes[index];
        text = str(boost::format("%s\n\nCost: %d %s\nRequires %s %d (yours: %d)%s") % entry.name % entry.cost %
                   materialName(entry.recipe.material) % skillName(_services, entry.recipe.skill) %
                   entry.recipe.skillRank % leader->getSkillValue(entry.recipe.skill) %
                   (entry.affordable ? "" : "\nNot enough material."));
    } else if (_mode == Mode::BreakDown && index < _breakable.size()) {
        const auto &item = *_breakable[index];
        auto material = crafting::materialOfBaseItem(item.baseItemType());
        text = str(boost::format("%s\n\nBreaks down into %d %s (%s %d)") % item.localizedName() %
                   _crafting.breakdownYieldFor(item, *leader) % materialName(material) %
                   skillName(_services, crafting::breakdownSkill(material)) %
                   leader->getSkillValue(crafting::breakdownSkill(material)));
    }
    _description->addTextLinesAsItems(text + "\n\n" + totals);
}

// --- UpgradeSlotsPanel -----------------------------------------------------

void UpgradeSlotsPanel::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    centerRootInCanvas(isTSL() ? 800 : 640, isTSL() ? 600 : 480);
    for (int i = 1; i <= 3; ++i) {
        _slots3.push_back({findControl<Button>(str(boost::format("BTN_UPGRADE3%d") % i)),
                           findControl<Label>(str(boost::format("LBL_UPGRADE3%d") % i))});
    }
    for (int i = 1; i <= 4; ++i) {
        auto button = findControl<Button>(str(boost::format("BTN_UPGRADE4%d") % i));
        auto label = findControl<Label>(str(boost::format("LBL_UPGRADE4%d") % i));
        if (!button) {
            // K2's layout draws lightsaber slots as its _LS set of six; a K1
            // lightsaber fills the first four.
            button = findControl<Button>(str(boost::format("BTN_UPGRADE3%d_LS") % i));
            label = findControl<Label>(str(boost::format("LBL_UPGRADE3%d_LS") % i));
        }
        _slots4.push_back({std::move(button), std::move(label)});
    }
    for (const char *tag : {"BTN_UPGRADE35_LS", "LBL_UPGRADE35_LS", "BTN_UPGRADE36_LS", "LBL_UPGRADE36_LS"}) {
        if (auto control = findControl<Control>(tag)) {
            control->setVisible(false);
        }
    }
    for (auto *group : {&_slots3, &_slots4}) {
        for (size_t i = 0; i < group->size(); ++i) {
            auto &button = (*group)[i].button;
            if (button) {
                button->setOnClick([this, i]() {
                    selectSlot(static_cast<int>(i));
                });
            }
        }
    }
    _list = findControl<ListBox>("LB_ITEMS");
    _description = findControl<ListBox>("LB_DESC");
    _descriptionSaber = findControl<ListBox>("LB_DESC_LS");
    _descBackground = findControl<Label>("LBL_DESCBG");
    _descBackgroundSaber = findControl<Label>("LBL_DESCBG_LS");
    _slotName = findControl<Label>("LBL_SLOTNAME");
    _saberSlotName = findControl<Label>("LBL_LSSLOTNAME");
    _property = findControl<Label>("LBL_PROPERTY");
    _count = findControl<Label>("LBL_UPGRADE_COUNT");
    _title = findControl<Label>("LBL_TITLE");
    _assemble = findControl<Button>("BTN_ASSEMBLE");
    // K2 authors OK on top of Assemble; the bench confirms with Assemble.
    if (auto ok = findControl<Button>("BTN_OK"); ok && _assemble) {
        ok->setVisible(false);
    }
    _back = findControl<Button>("BTN_BACK");
    for (const char *tag : {"3D_MODEL", "3D_MODEL_LS"}) {
        if (auto model = findControl<Control>(tag)) {
            model->setVisible(false);
        }
    }
    if (_list) {
        configureItemList(*_list, _baseColor, _hilightColor);
        _list->setOnItemDoubleClick([this](const std::string &) {
            activateRow(_list->selectedItemIndex());
        });
    }
    for (auto &description : {_description, _descriptionSaber}) {
        if (description) {
            description->setProtoMatchContent(true);
        }
    }
    if (_assemble) {
        _assemble->setOnClick([this]() {
            _host.assemble();
        });
    }
    if (_back) {
        _back->setOnClick([this]() {
            _host.back();
        });
    }
}

std::string UpgradeSlotsPanel::slotName(const UpgradeSlot &slot) const {
    auto &strings = _services.resource.strings;
    auto item = _host.bench().item();
    if (item && item->isLightsaber()) {
        return strings.getText(slot.color ? kColorCrystalStrRef : kPowerCrystalStrRef);
    }
    if (slot.type >= 0) {
        // The slot is named after the upgrade kind it takes.
        for (const auto &candidate : _host.bench().table().kinds()) {
            if (candidate.type == slot.type && !candidate.templateResRef.empty()) {
                return _host.templateLook(candidate.templateResRef).second;
            }
        }
    }
    return "";
}

void UpgradeSlotsPanel::show() {
    _selectedSlot = 0;
    refresh();
}

void UpgradeSlotsPanel::selectSlot(int slot) {
    _selectedSlot = slot;
    refresh();
}

void UpgradeSlotsPanel::refresh() {
    UpgradeBench &bench = _host.bench();
    auto item = bench.item();
    auto slots = bench.slots();
    bool saber = item && item->isLightsaber();
    _active = slots.size() > 3 ? &_slots4 : &_slots3;
    if (_selectedSlot >= static_cast<int>(slots.size())) {
        _selectedSlot = 0;
    }

    for (auto *group : {&_slots3, &_slots4}) {
        for (size_t i = 0; i < group->size(); ++i) {
            bool visible = group == _active && i < slots.size();
            auto &controls = (*group)[i];
            if (controls.button) {
                controls.button->setVisible(visible);
                controls.button->setSelected(visible && static_cast<int>(i) == _selectedSlot);
            }
            if (controls.label) {
                controls.label->setVisible(visible);
                std::shared_ptr<Texture> icon;
                if (visible) {
                    const UpgradeSlot &slot = slots[i];
                    std::string resRef;
                    if (slot.color) {
                        resRef = bench.colorCrystalFor(bench.stagedModelVariation());
                    } else if (const UpgradeKind *kind = bench.table().kind(slot.installedRow)) {
                        resRef = kind->templateResRef;
                    }
                    if (!resRef.empty()) {
                        icon = _host.templateLook(resRef).first;
                    }
                }
                controls.label->setBorderFill(icon);
            }
        }
    }

    // The lightsaber layout names the slot under the saber; the others above.
    std::string name = _selectedSlot < static_cast<int>(slots.size()) ? slotName(slots[_selectedSlot]) : "";
    if (_saberSlotName) {
        _saberSlotName->setVisible(saber);
        _saberSlotName->setTextMessage(name);
    }
    if (_slotName) {
        _slotName->setVisible(!saber);
        _slotName->setTextMessage(item ? item->localizedName() : "");
    }
    if (_property) {
        _property->setVisible(!saber);
        _property->setTextMessage(saber ? "" : name);
    }
    if (_title && isTSL()) {
        // K2's title line is the item name (its bench title is LBL_TITLE2).
        _title->setTextMessage(item ? item->localizedName() : "");
    }
    for (auto &control : {std::static_pointer_cast<Control>(_descriptionSaber),
                          std::static_pointer_cast<Control>(_descBackgroundSaber)}) {
        if (control) {
            control->setVisible(saber);
        }
    }
    for (auto &control : {std::static_pointer_cast<Control>(_description),
                          std::static_pointer_cast<Control>(_descBackground)}) {
        if (control) {
            control->setVisible(!saber);
        }
    }
    if (_count) {
        int installed = 0;
        for (const auto &slot : slots) {
            installed += (!slot.color && slot.installedRow >= 0) ? 1 : 0;
        }
        _count->setTextMessage(std::to_string(installed));
    }

    // Candidates for the selected slot, after what is in it.
    _rows.clear();
    if (_list) {
        _list->clearItems();
        if (_selectedSlot < static_cast<int>(slots.size())) {
            const UpgradeSlot &slot = slots[_selectedSlot];
            if (!slot.color && slot.installedRow >= 0) {
                if (const UpgradeKind *kind = bench.table().kind(slot.installedRow)) {
                    auto look = _host.templateLook(kind->templateResRef);
                    ListBox::Item row;
                    row.tag = "installed";
                    row.text = look.second;
                    row.iconTexture = look.first;
                    row.textColor = _hilightColor;
                    _list->addItem(std::move(row));
                    _rows.push_back({true, nullptr});
                }
            }
            for (const auto &candidate : bench.candidates(_selectedSlot)) {
                ListBox::Item row;
                row.tag = std::to_string(candidate->id());
                row.text = candidate->localizedName();
                row.iconTexture = candidate->icon();
                row.iconFrame = itemFrameTexture(candidate->stackSize());
                if (candidate->stackSize() > 1) {
                    row.iconText = std::to_string(candidate->stackSize());
                }
                _list->addItem(std::move(row));
                _rows.push_back({false, candidate});
            }
        }
        _list->setSelectedItemIndex(_rows.empty() ? -1 : 0);
    }

    auto description = saber ? _descriptionSaber : _description;
    if (description) {
        description->clearItems();
        if (item) {
            // Describe the item as it would be with the staged slots.
            uint32_t actual = item->upgrades();
            item->setUpgrades(bench.stagedUpgrades());
            description->addTextLinesAsItems(joinItemDescriptionLines(buildItemDescriptionLines(*item, _services)));
            item->setUpgrades(actual);
        }
    }
    if (_assemble) {
        _assemble->setDisabled(!bench.isDirty());
    }
}

void UpgradeSlotsPanel::activateRow(int index) {
    if (index < 0 || index >= static_cast<int>(_rows.size())) {
        return;
    }
    UpgradeBench &bench = _host.bench();
    const Row &row = _rows[index];
    if (row.installed) {
        bench.remove(_selectedSlot);
    } else {
        bench.install(_selectedSlot, row.candidate);
    }
    refresh();
}

// --- UpgradeGUI ------------------------------------------------------------

UpgradeGUI::UpgradeGUI(Game &game, ServicesView &services) :
    GameGUI(game, services),
    _bench(game, services) {
    _resRef = guiResRef("upgradesel");
}

void UpgradeGUI::init() {
    GameGUI::init();
    _itemsPanel = std::make_unique<UpgradeItemsPanel>(*this, _game, _services);
    _itemsPanel->init();
    _slotsPanel = std::make_unique<UpgradeSlotsPanel>(*this, _game, _services);
    _slotsPanel->init();
    _craftingPanel = std::make_unique<CraftingPanel>(*this, _game, _services);
    _craftingPanel->init();
}

bool UpgradeGUI::craftingAvailable() const {
    return _services.resource.k2Merge != nullptr;
}

void UpgradeGUI::openCrafting(CraftingPanel::Mode mode) {
    if (!craftingAvailable()) {
        return;
    }
    _bench.cancel();
    _page = Page::Crafting;
    _craftingPanel->show(mode);
}

struct CategoryChoice {
    const char *tag;
    UpgradeCategory category;
};

static constexpr CategoryChoice kCategoryChoices[] {
    {"BTN_LIGHTSABER", UpgradeCategory::Lightsaber},
    {"BTN_RANGED", UpgradeCategory::Ranged},
    {"BTN_MELEE", UpgradeCategory::Melee},
    {"BTN_ARMOR", UpgradeCategory::Armor},
    {"BTN_ALL", UpgradeCategory::None}};

void UpgradeGUI::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    centerRootInCanvas(isTSL() ? 800 : 640, isTSL() ? 600 : 480);
    _selectList = findControl<ListBox>("LB_UPGRADELIST");
    _selectDescription = findControl<ListBox>("LB_DESCRIPTION");
    // K2's Create Items: shown with the K2 systems merged (crafting), else
    // hidden as a K2-only control.
    if (auto create = findControl<Button>("BTN_CREATEITEMS"); create && craftingAvailable()) {
        create->setOnClick([this]() {
            openCrafting(CraftingPanel::Mode::Create);
        });
    } else {
        hideK2OnlyControls({"BTN_CREATEITEMS"});
    }
    if (!_selectList) {
        hideK2OnlyControls({"BTN_ALL"});
    }
    if (_selectList) {
        configureItemList(*_selectList, _baseColor, _hilightColor);
        _selectList->setOnItemClick([this](const std::string &) {
            describeSelectItem();
        });
        _selectList->setOnItemDoubleClick([this](const std::string &) {
            if (auto item = selectedSelectItem()) {
                openItem(item);
            }
        });
    }
    if (_selectDescription) {
        _selectDescription->setProtoMatchContent(true);
    }
    for (const auto &choice : kCategoryChoices) {
        auto button = findControl<Button>(choice.tag);
        if (!button) {
            continue;
        }
        UpgradeCategory category = choice.category;
        button->setOnClick([this, category]() {
            _category = category;
            showCategoryChoice();
            if (_selectList) {
                refreshSelectList();
            }
        });
    }
    if (auto button = findControl<Button>("BTN_UPGRADEITEMS")) {
        button->setOnClick([this]() {
            if (_selectList) {
                if (auto item = selectedSelectItem()) {
                    openItem(item);
                }
                return;
            }
            openCategory(_category);
        });
    }
    if (auto button = findControl<Button>("BTN_BACK")) {
        button->setOnClick([this]() {
            close();
        });
    }
}

void UpgradeGUI::showCategoryChoice() {
    // The pick stays marked by its text colour: hover moves the button
    // selection, so selection alone would not hold it.
    for (const auto &choice : kCategoryChoices) {
        auto control = findControl<Button>(choice.tag);
        if (!control) {
            continue;
        }
        bool chosen = choice.category == _category;
        if (isTSL()) {
            updateK2FilterButton(control, chosen);
        } else {
            control->setSelected(chosen);
            control->setTextColor(chosen ? _hilightColor : _baseColor);
        }
    }
}

void UpgradeGUI::refreshSelectList() {
    _selectItems = _bench.partyItems(_category);
    if (!_selectList) {
        return;
    }
    _selectList->clearItems();
    for (const auto &item : _selectItems) {
        ListBox::Item row;
        row.tag = std::to_string(item->id());
        row.text = item->localizedName();
        row.iconTexture = item->icon();
        row.iconFrame = itemFrameTexture(item->stackSize());
        _selectList->addItem(std::move(row));
    }
    _selectList->setSelectedItemIndex(_selectItems.empty() ? -1 : 0);
    if (auto button = findControl<Button>("BTN_UPGRADEITEMS")) {
        button->setDisabled(_selectItems.empty());
    }
    describeSelectItem();
}

std::shared_ptr<Item> UpgradeGUI::selectedSelectItem() const {
    int index = _selectList ? _selectList->selectedItemIndex() : -1;
    if (index < 0 || index >= static_cast<int>(_selectItems.size())) {
        return nullptr;
    }
    return _selectItems[index];
}

void UpgradeGUI::describeSelectItem() {
    if (!_selectDescription) {
        return;
    }
    _selectDescription->clearItems();
    if (auto item = selectedSelectItem()) {
        _selectDescription->addTextLinesAsItems(joinItemDescriptionLines(buildItemDescriptionLines(*item, _services)));
    }
}

void UpgradeGUI::returnToItemList() {
    if (_selectList) {
        _page = Page::Select;
        refreshSelectList();
        return;
    }
    _page = Page::Items;
    _itemsPanel->refresh();
}

GameGUI *UpgradeGUI::pageGUI() {
    switch (_page) {
    case Page::Items:
        return _itemsPanel.get();
    case Page::Slots:
        return _slotsPanel.get();
    case Page::Crafting:
        return _craftingPanel.get();
    default:
        return nullptr;
    }
}

bool UpgradeGUI::handle(const input::Event &event) {
    if (isEscape(event)) {
        back();
        return true;
    }
    if (auto page = pageGUI()) {
        return page->handle(event);
    }
    return GameGUI::handle(event);
}

void UpgradeGUI::update(float dt) {
    if (auto page = pageGUI()) {
        page->update(dt);
        return;
    }
    GameGUI::update(dt);
}

void UpgradeGUI::render() {
    if (auto page = pageGUI()) {
        page->render();
        return;
    }
    GameGUI::render();
}

void UpgradeGUI::open(const std::shared_ptr<Item> &item) {
    _bench.cancel();
    _directItem = static_cast<bool>(item);
    if (item) {
        openItem(item);
        return;
    }
    _page = Page::Select;
    if (_selectList) {
        // K2 opens its workbench on every item.
        _category = UpgradeCategory::None;
        showCategoryChoice();
        refreshSelectList();
        return;
    }
    if (_category == UpgradeCategory::None) {
        _category = UpgradeCategory::Lightsaber;
    }
    showCategoryChoice();
}

void UpgradeGUI::openCategory(UpgradeCategory category) {
    _category = category == UpgradeCategory::None ? UpgradeCategory::Lightsaber : category;
    _bench.cancel();
    _page = Page::Items;
    _itemsPanel->show(_category);
}

void UpgradeGUI::openItem(const std::shared_ptr<Item> &item) {
    if (!_bench.begin(item)) {
        return;
    }
    _page = Page::Slots;
    _slotsPanel->show();
}

void UpgradeGUI::back() {
    switch (_page) {
    case Page::Slots:
        _bench.cancel();
        if (_directItem) {
            close();
            return;
        }
        returnToItemList();
        break;
    case Page::Items:
        _page = Page::Select;
        break;
    case Page::Crafting:
        returnToItemList();
        break;
    default:
        close();
        break;
    }
}

bool UpgradeGUI::assemble() {
    bool applied = _game.assembleUpgrades();
    if (_directItem) {
        close();
        return applied;
    }
    returnToItemList();
    return applied;
}

void UpgradeGUI::close() {
    _bench.cancel();
    _directItem = false;
    _page = Page::Select;
    _game.closeUpgradeScreen();
}

std::pair<std::shared_ptr<Texture>, std::string> UpgradeGUI::templateLook(const std::string &resRef) const {
    std::pair<std::shared_ptr<Texture>, std::string> result;
    auto uti = _services.resource.gffs.get(resRef, ResType::Uti);
    if (!uti) {
        return result;
    }
    int baseItem = uti->getInt("BaseItem", -1);
    int variation = static_cast<int>(uti->getUint("ModelVariation", 1));
    if (auto baseItems = _services.resource.twoDas.get("baseitems"); baseItems && baseItem >= 0) {
        auto itemClass = boost::to_lower_copy(baseItems->getString(baseItem, "itemclass"));
        if (!itemClass.empty()) {
            result.first = _services.resource.textures.get(
                str(boost::format("i%s_%03d") % itemClass % variation), TextureUsage::GUI);
        }
    }
    LocString name;
    if (uti->readLocString(name, "LocalizedName", _services.resource.strings)) {
        result.second = name.str();
    }
    return result;
}

} // namespace game

} // namespace reone
