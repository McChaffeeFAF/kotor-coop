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

#include "reone/game/gui/hud.h"

#include "reone/audio/mixer.h"
#include "reone/game/coopsession.h"
#include "reone/graphics/context.h"
#include "reone/graphics/font.h"
#include "reone/resource/provider/fonts.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/uniforms.h"
#include "reone/gui/control/label.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/strings.h"
#include "reone/system/logutil.h"

#include "reone/game/action/attackobject.h"
#include "reone/game/combatqueue.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/switchweapons.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/creaturestates.h"
#include "reone/game/d20/feat.h"
#include "reone/game/d20/feats.h"
#include "reone/game/d20/spell.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/area.h"
#include "reone/game/object/camera.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

static std::string g_attackIcon("i_attack");
static std::string g_switchWeaponsIcon("i_swtchwpn");

/** Authored-canvas gap between the minimap frame and the TSL bark bubble. */
static constexpr int kBarkBubbleMapGap = 8;

static void tintK2HUDMenuButton(const std::shared_ptr<Button> &button, const glm::vec3 &baseColor) {
    if (!button) {
        return;
    }
    button->setBorderColor(baseColor);
    button->setTintBorderFill(true);
}

void HUD::preload(IGUI &gui) {
    gui.setResolution(800, 600);
    gui.setScaling(GUI::ScalingMode::PositionRelativeToCenter);

    static constexpr const char *kCentredCombatControls[] = {
        "BTN_CLEARALL", "BTN_CLEARONE", "BTN_CLEARONE2",
        "LBL_CMBTMODEMSG", "LBL_CMBTMSGBG", "LBL_COMBATBG1", "LBL_COMBATBG2", "LBL_COMBATBG3",
        "LBL_QUEUE0", "LBL_QUEUE1", "LBL_QUEUE2", "LBL_QUEUE3"};
    for (auto tag : kCentredCombatControls) {
        gui.setControlScaling(tag, GUI::ScalingMode::ScaledRelativeToCenter);
    }
}

template <class T>
static void setVisible(const std::shared_ptr<T> &control, bool visible) {
    if (control) {
        control->setVisible(visible);
    }
}

template <class T>
static void setOnClick(const std::shared_ptr<T> &control, std::function<void()> fn) {
    if (control) {
        control->setOnClick(std::move(fn));
    }
}

static void completeFirstAttack(Creature *leader) {
    if (!leader) {
        return;
    }
    if (auto *coop = leader->game().coop(); coop && coop->isClient()) {
        coop->submitRequest(net::CommandType::Cancel, 0, 2); // the host clears it
        return;
    }
    for (auto &action : leader->actions()) {
        if (action->type() == ActionType::AttackObject) {
            action->complete();
            break;
        }
    }
}

std::vector<std::string> HUD::expectedTags(bool tsl) {
    // The tags this HUD binds by name and that its layout authors. K1 and K2
    // share the rest; any one may be missing without the HUD failing.
    std::vector<std::string> tags {
        "BTN_ABI", "BTN_CHAR", "BTN_CHAR1", "BTN_CHAR2", "BTN_CHAR3", "BTN_CLEARALL", "BTN_CLEARONE",
        "BTN_CLEARONE2", "BTN_EQU", "BTN_INV", "BTN_JOU", "BTN_MAP", "BTN_MINIMAP", "BTN_MSG", "BTN_OPT",
        "BTN_TARGET0", "BTN_TARGET1", "BTN_TARGET2", "BTN_TARGETDOWN0", "BTN_TARGETDOWN1", "BTN_TARGETDOWN2",
        "BTN_TARGETUP0", "BTN_TARGETUP1", "BTN_TARGETUP2", "LBL_ARROW", "LBL_ARROW_MARGIN", "LBL_BACK1",
        "LBL_BACK2", "LBL_BACK3", "LBL_CASH", "LBL_CHAR1", "LBL_CHAR2", "LBL_CHAR3", "LBL_CMBTEFCTINC1",
        "LBL_CMBTEFCTINC2", "LBL_CMBTEFCTINC3", "LBL_CMBTEFCTRED1", "LBL_CMBTEFCTRED2", "LBL_CMBTEFCTRED3",
        "LBL_CMBTMODEMSG", "LBL_CMBTMSGBG", "LBL_COMBATBG3", "LBL_DARKSHIFT", "LBL_DEBILATATED1",
        "LBL_DEBILATATED2", "LBL_DEBILATATED3", "LBL_DISABLE1", "LBL_DISABLE2", "LBL_DISABLE3", "LBL_HEALTHBG",
        "LBL_ITEMLOST", "LBL_ITEMRCVD", "LBL_JOURNAL", "LBL_LEVELUP1", "LBL_LEVELUP2", "LBL_LEVELUP3",
        "LBL_LIGHTSHIFT", "LBL_MAP", "LBL_MAPBORDER", "LBL_MAPVIEW", "LBL_MENUBG", "LBL_MOULDING1",
        "LBL_MOULDING3", "LBL_NAME", "LBL_NAMEBG", "LBL_PLOTXP", "LBL_QUEUE0", "LBL_QUEUE1", "LBL_QUEUE2",
        "LBL_QUEUE3", "LBL_STEALTHXP", "LBL_TARGET0", "LBL_TARGET1", "LBL_TARGET2", "PB_FORCE1", "PB_FORCE2",
        "PB_FORCE3", "PB_HEALTH", "PB_VIT1", "PB_VIT2", "PB_VIT3", "TB_PAUSE", "TB_SOLO", "TB_STEALTH",
        "LBL_ACTIONDESC", "LBL_ACTIONDESCBG"};
    for (int i = 0; i < (tsl ? 6 : 4); ++i) {
        for (auto prefix : {"BTN_ACTION", "LBL_ACTION", "BTN_ACTIONUP", "BTN_ACTIONDOWN"}) {
            tags.push_back(prefix + std::to_string(i));
        }
    }
    if (tsl) {
        tags.push_back("BTN_SWAPWEAPONS");
    } else {
        for (auto tag : {"LBL_COMBATBG1", "LBL_COMBATBG2", "LBL_LVLUPBG1", "LBL_LVLUPBG2", "LBL_LVLUPBG3", "LBL_MOULDING2"}) {
            tags.push_back(tag);
        }
    }
    return tags;
}

void HUD::addAutoCombatButton() {
    if (!isTSL()) {
        // K1 layout: the toggle joins the weapon-set controls left of the
        // quick bar, in K1 art (K1HUDControls).
        auto firstSlot = findControl<gui::Button>("BTN_ACTION0");
        _k1Controls = std::make_unique<K1HUDControls>(*_gui);
        _k1Controls->build(
            firstSlot ? firstSlot->authoredExtent() : K1HUDControls::defaultFirstQuickSlot(),
            [this]() {
                if (auto leader = _game.party().getLeader()) {
                    requestWeaponSwitch(_game, *leader);
                }
            },
            [this]() {
                _game.toggleLeaderAutoCombat();
            });
        return;
    }
    std::shared_ptr<Control> anchor = isTSL() && _controls.BTN_SWAPWEAPONS
                                          ? std::static_pointer_cast<Control>(_controls.BTN_SWAPWEAPONS)
                                          : std::static_pointer_cast<Control>(_controls.BTN_CLEARALL);
    if (!anchor) {
        return;
    }
    auto button = std::shared_ptr<Control>(_gui->newControl(ControlType::Button, "BTN_AUTOCOMBAT"));
    button->setBorder(anchor->border());
    button->setTintBorderFill(isTSL());
    button->setText(anchor->text());
    button->setTextMessage("AI");
    // Authored coordinates, so every layout pass places it as it places the
    // anchor.
    auto extent = anchor->authoredExtent();
    const int gap = std::max(2, extent.width / 8);
    extent.left -= extent.width + gap;
    button->setAuthoredExtent(extent);
    button->setExtent(extent);
    button->setOnClick([this]() {
        _game.toggleLeaderAutoCombat();
    });
    _gui->addControlToFront(button, IGUI::ControlCoordinates::Authored);
    _autoCombatButton = std::move(button);
    _autoCombatButton->setVisible(false);
}

void HUD::refreshAutoCombatButton(bool visible) {
    if (!_autoCombatButton) {
        return;
    }
    _autoCombatButton->setVisible(visible);
    auto leader = _game.party().getLeader();
    bool on = leader && leader->autoCombat();
    // Lit while on, dimmed while off.
    _autoCombatButton->setTextColor(on ? glm::vec3(0.4f, 1.0f, 0.4f) : glm::vec3(0.6f, 0.6f, 0.6f));
}

void HUD::onGUILoaded() {
    bindControls();
    addAutoCombatButton();
    addEffectIcons();

    // Both layouts ship the literal placeholder "Combat mode message" in this
    // label; it only ever shows text the game gives it.
    if (_controls.LBL_CMBTMODEMSG) {
        _controls.LBL_CMBTMODEMSG->setTextMessage("");
    }

    // Every branch below keys off the presentation (isTSL()), never the
    // running game: a K1 session can present the K2 layout. Any control the
    // loaded layout lacks is simply skipped.
    if (!isTSL()) {
        // The GUI control tree outlives this wrapper in the GUI cache. Derive
        // from the live layout inputs so rebuilding a HUD cannot feed the
        // previous presentation scale back into itself.
        const float combatTextScale = _gui->scale() * _gui->textScale() * kK1CombatTextScale;
        for (Control *control : {
                 static_cast<Control *>(_controls.LBL_CMBTMODEMSG.get()),
                 static_cast<Control *>(_controls.BTN_CLEARALL.get()),
                 static_cast<Control *>(_controls.BTN_CLEARONE.get()),
                 static_cast<Control *>(_controls.BTN_CLEARONE2.get())}) {
            if (control) {
                control->setScale(combatTextScale);
            }
        }
        if (auto actionDescription = findControl<gui::Label>("LBL_ACTIONDESC")) {
            actionDescription->setScale(combatTextScale);
        }
    }

    // The menu strip and the action queue are icon artwork, magnified on any
    // modern screen, so they get their alpha edge reconstructed.
    for (auto &icon : {_controls.BTN_EQU, _controls.BTN_INV, _controls.BTN_CHAR,
                       _controls.BTN_ABI, _controls.BTN_MSG, _controls.BTN_JOU,
                       _controls.BTN_MAP, _controls.BTN_OPT}) {
        if (icon) {
            icon->setSharpenBorderFillAlpha(true);
        }
    }
    for (auto &icon : {_controls.LBL_QUEUE0, _controls.LBL_QUEUE1,
                       _controls.LBL_QUEUE2, _controls.LBL_QUEUE3}) {
        if (icon) {
            icon->setSharpenBorderFillAlpha(true);
        }
    }

    _actionBar.addDescription(
        findControl<gui::Label>("LBL_ACTIONDESC"),
        findControl<gui::Label>("LBL_ACTIONDESCBG"));

    for (int i = 0; i < 5; ++i) {
        auto action = findControl<gui::Button>(str(boost::format("BTN_ACTION%d") % i));
        auto icon = findControl<gui::Button>(str(boost::format("LBL_ACTION%d") % i));
        auto up = findControl<gui::Button>(str(boost::format("BTN_ACTIONUP%d") % i));
        auto down = findControl<gui::Button>(str(boost::format("BTN_ACTIONDOWN%d") % i));

        if (action && icon && up && down) {
            _actionBar.addSlot(action, icon, up, down);
        }
    }
    if (isTSL()) {
        if (auto up = findControl<gui::Button>("BTN_ACTIONUP5")) {
            up->setVisible(false);
        }
        if (auto down = findControl<gui::Button>("BTN_ACTIONDOWN5")) {
            down->setBorderFillTransform(gui::Control::Border::FillTransform::Rotate180);
            down->setHilightFillTransform(gui::Control::Border::FillTransform::Rotate180);
            down->setVisible(false);
        }
    }

    for (auto control : std::initializer_list<std::shared_ptr<Control>> {
             _controls.BTN_CLEARALL, _controls.BTN_TARGET0, _controls.BTN_TARGET1, _controls.BTN_TARGET2,
             _controls.BTN_TARGETDOWN0, _controls.BTN_TARGETDOWN1, _controls.BTN_TARGETDOWN2,
             _controls.BTN_TARGETUP0, _controls.BTN_TARGETUP1, _controls.BTN_TARGETUP2,
             _controls.LBL_ARROW_MARGIN, _controls.LBL_CASH,
             _controls.LBL_CMBTEFCTINC1, _controls.LBL_CMBTEFCTINC2, _controls.LBL_CMBTEFCTINC3,
             _controls.LBL_CMBTEFCTRED1, _controls.LBL_CMBTEFCTRED2, _controls.LBL_CMBTEFCTRED3,
             _controls.LBL_CMBTMODEMSG, _controls.LBL_CMBTMSGBG, _controls.LBL_COMBATBG3,
             _controls.LBL_DARKSHIFT, _controls.LBL_DEBILATATED1, _controls.LBL_DEBILATATED2,
             _controls.LBL_DEBILATATED3, _controls.LBL_DISABLE1, _controls.LBL_DISABLE2, _controls.LBL_DISABLE3,
             _controls.LBL_JOURNAL, _controls.LBL_HEALTHBG, _controls.LBL_ITEMRCVD, _controls.LBL_ITEMLOST,
             _controls.LBL_LIGHTSHIFT, _controls.LBL_MAP, _controls.LBL_MOULDING1, _controls.LBL_MOULDING3,
             _controls.LBL_NAME, _controls.LBL_NAMEBG, _controls.LBL_PLOTXP, _controls.LBL_STEALTHXP,
             _controls.LBL_ARROW}) {
        setVisible(control, false);
    }
    // Pause, solo mode and Stealth Mode are live (design note k1hud_parity_2,
    // design note stealth): their SELECTED art shows the state. K1's mipc2* layouts
    // author TB_STEALTH right of solo; its mipc* ones get one there.
    addK1StealthToggle();
    for (auto &toggle : {_controls.TB_PAUSE, _controls.TB_SOLO, _controls.TB_STEALTH}) {
        if (toggle) {
            toggle->setShowCheckedFill(true);
            toggle->setSharpenBorderFillAlpha(true);
        }
    }
    setOnClick(_controls.TB_PAUSE, [this]() {
        _game.togglePauseAsKey();
    });
    setOnClick(_controls.TB_SOLO, [this]() {
        _game.toggleSoloModeAsKey();
    });
    setOnClick(_controls.TB_STEALTH, [this]() {
        _game.toggleStealthAsKey();
    });
    setVisible(_controls.TB_STEALTH, false);

    if (isTSL()) {
        tintK2HUDMenuButton(_controls.BTN_EQU, _baseColor);
        tintK2HUDMenuButton(_controls.BTN_INV, _baseColor);
        tintK2HUDMenuButton(_controls.BTN_CHAR, _baseColor);
        tintK2HUDMenuButton(_controls.BTN_ABI, _baseColor);
        tintK2HUDMenuButton(_controls.BTN_MSG, _baseColor);
        tintK2HUDMenuButton(_controls.BTN_JOU, _baseColor);
        tintK2HUDMenuButton(_controls.BTN_MAP, _baseColor);
        tintK2HUDMenuButton(_controls.BTN_OPT, _baseColor);
        tintK2HUDMenuButton(_controls.BTN_SWAPWEAPONS, _baseColor);
        // The portrait frames and the quick-bar arrows are colour masks too;
        // untinted they draw white and khaki.
        for (auto &frame : {_controls.LBL_BACK1, _controls.LBL_BACK2, _controls.LBL_BACK3}) {
            if (frame) {
                frame->setBorderColor(_baseColor);
                frame->setTintBorderFill(true);
            }
        }
        for (int i = 0; i < 6; ++i) {
            for (auto prefix : {"BTN_ACTIONUP", "BTN_ACTIONDOWN"}) {
                if (auto arrow = findControl<gui::Button>(prefix + std::to_string(i))) {
                    arrow->setBorderColor(_baseColor);
                    arrow->setHilightColor(_hilightColor);
                    arrow->setTintBorderFill(true);
                }
            }
        }
        // The bar backings are colour masks like the rest of the TSL HUD.
        for (auto &bar : {_controls.PB_VIT1, _controls.PB_VIT2, _controls.PB_VIT3,
                          _controls.PB_FORCE1, _controls.PB_FORCE2, _controls.PB_FORCE3}) {
            if (bar) {
                bar->setTintBorderFill(true);
            }
        }
    } else {
        setVisible(_controls.LBL_COMBATBG1, false);
        setVisible(_controls.LBL_COMBATBG2, false);
    }

    setOnClick(_controls.BTN_EQU, [this]() {
        _game.openInGameMenu(InGameMenuTab::Equipment);
    });
    setOnClick(_controls.BTN_INV, [this]() {
        _game.openInGameMenu(InGameMenuTab::Inventory);
    });
    setOnClick(_controls.BTN_CHAR, [this]() {
        _game.openInGameMenu(InGameMenuTab::Character);
    });
    setOnClick(_controls.BTN_ABI, [this]() {
        _game.openInGameMenu(InGameMenuTab::Abilities);
    });
    setOnClick(_controls.BTN_MSG, [this]() {
        // K2's menu bar puts Party here; its screen lists K1's roster in a K1
        // session (design note p1_k2menus). K1's opens Messages.
        if (isTSL()) {
            _game.openInGameMenu(InGameMenuTab::Party);
        } else {
            _game.openInGameMenu(InGameMenuTab::Messages);
        }
    });
    setOnClick(_controls.BTN_JOU, [this]() {
        _game.openInGameMenu(InGameMenuTab::Journal);
    });
    setOnClick(_controls.BTN_MAP, [this]() {
        _game.openInGameMenu(InGameMenuTab::Map);
    });
    setOnClick(_controls.BTN_OPT, [this]() {
        _game.openInGameMenu(InGameMenuTab::Options);
    });
    setOnClick(_controls.BTN_SWAPWEAPONS, [this]() {
        if (auto leader = _game.party().getLeader()) {
            requestWeaponSwitch(_game, *leader);
        }
    });
    setOnClick(_controls.BTN_CLEARALL, [this]() {
        // Cancel Combat: clear the queue and leave combat mode.
        if (auto *coop = _game.coop(); coop && coop->isClient()) {
            coop->submitRequest(net::CommandType::Cancel, 0, 1);
            return;
        }
        if (auto leader = _game.party().getLeader()) {
            leader->cancelCombatNow();
        }
    });
    setOnClick(_controls.BTN_CLEARONE, [this]() {
        completeFirstAttack(_game.party().getLeader().get());
    });
    setOnClick(_controls.BTN_CLEARONE2, [this]() {
        completeFirstAttack(_game.party().getLeader().get());
    });
    setOnClick(_controls.BTN_CHAR1, [this]() {
        _game.openInGameMenu(InGameMenuTab::Equipment);
    });
    setOnClick(_controls.BTN_CHAR2, [this]() {
        _game.party().playerSetLeaderByIndex(1);
    });
    setOnClick(_controls.BTN_CHAR3, [this]() {
        _game.party().playerSetLeaderByIndex(2);
    });

    _select.init();

    _barkBubble = std::make_unique<BarkBubble>(_game, _services);
    if (isTSL() && _controls.LBL_MAPBORDER) {
        // TSL authors the bark bubble over the minimap frame. Keep it below;
        // both GUIs share the 800x600 authored canvas.
        const auto &mapBorder = _controls.LBL_MAPBORDER->authoredExtent();
        _barkBubble->setAuthoredTop(mapBorder.top + mapBorder.height + kBarkBubbleMapGap);
    }
    _barkBubble->init();

    _statusSummary = std::make_unique<StatusSummary>(_game, _services, _game.statusSummary());
    _statusSummary->init();

    _areaTransition = std::make_unique<AreaTransition>(_game, _services);
    _areaTransition->init();
}

std::shared_ptr<AudioClip> HUD::guiSound(const std::string &label) const {
    // The running game's guisounds.2da first, then the HUD profile's: K1
    // ships no alignment rows, K2 does (LightSide, DarkSide).
    auto presentation = _game.hudPresentation();
    for (resource::ResourceServices *resources : {&_services.resource, &presentation.resource}) {
        auto table = resources->twoDas.get("guisounds");
        if (!table) {
            continue;
        }
        int row = table->indexByCellValue("label", label);
        if (row == -1) {
            continue;
        }
        if (auto clip = resources->audioClips.get(table->getString(row, "soundresref"))) {
            return clip;
        }
    }
    return nullptr;
}

std::array<Label *, kStatusSummaryCategoryCount> HUD::indicatorLabels() const {
    return {{_controls.LBL_JOURNAL.get(), _controls.LBL_CASH.get(), _controls.LBL_PLOTXP.get(),
             _controls.LBL_STEALTHXP.get(), _controls.LBL_DARKSHIFT.get(), _controls.LBL_LIGHTSHIFT.get(),
             _controls.LBL_ITEMRCVD.get(), _controls.LBL_ITEMLOST.get()}};
}

// Plot and stealth XP share one HUD spot, as do the two alignment flashes:
// lighting one puts out the other (design note p2_ui, "HUD flashes").
static std::optional<StatusSummaryCategory> sharedIndicatorSpot(StatusSummaryCategory category) {
    switch (category) {
    case StatusSummaryCategory::PlotXP:
        return StatusSummaryCategory::StealthXP;
    case StatusSummaryCategory::StealthXP:
        return StatusSummaryCategory::PlotXP;
    case StatusSummaryCategory::DarkSideShift:
        return StatusSummaryCategory::LightSideShift;
    case StatusSummaryCategory::LightSideShift:
        return StatusSummaryCategory::DarkSideShift;
    default:
        return std::nullopt;
    }
}

void HUD::activateStatusSummaryIndicator(StatusSummaryCategory category) {
    auto labels = indicatorLabels();
    size_t index = static_cast<size_t>(category);
    if (index >= labels.size() || !labels[index]) {
        return;
    }
    if (auto other = sharedIndicatorSpot(category)) {
        size_t otherIndex = static_cast<size_t>(*other);
        _indicators[otherIndex].reset();
        if (labels[otherIndex]) {
            labels[otherIndex]->setVisible(false);
        }
    }
    _indicators[index].activate();
    labels[index]->setVisible(true);
}

void HUD::updateStatusSummaryIndicators(float dt) {
    auto labels = indicatorLabels();
    for (size_t i = 0; i < _indicators.size(); ++i) {
        _indicators[i].update(dt);
        if (labels[i]) {
            labels[i]->setVisible(_indicators[i].visible());
        }
    }
}

void HUD::resetStatusSummaryPresentation() {
    auto labels = indicatorLabels();
    for (size_t i = 0; i < _indicators.size(); ++i) {
        _indicators[i].reset();
        if (labels[i]) {
            labels[i]->setVisible(false);
        }
    }
    if (_statusSummary) {
        _statusSummary->reset();
    }
}

void HUD::activateTargetAction(int index) {
    _select.activateActionSlot(index);
}

void HUD::activatePersonalAction(int index) {
    // Retail key order: power, medical, other, mines; the bar's slots are
    // power, medical, other (defensive), mines.
    _actionBar.activateSlot(index);
}

bool HUD::handle(const input::Event &event) {
    if (_statusSummary && _statusSummary->isVisible()) {
        return _statusSummary->handle(event);
    }
    if (handleCoopPortraitClick(event)) {
        return true;
    }
    if (_select.handle(event)) {
        return true;
    }
    return _gui->handle(event);
}

HUD::EffectIconRow HUD::effectIconRowFor(const Control::Extent &mainPortrait, bool tsl) {
    // The leader's effect icons, a row of kEffectIconSize squares in the
    // layout's authored space (design note k1hud_parity_2). K1: above the two small
    // portraits, right of the toggle column, reading left to right. K2: above
    // the main portrait's left, beside the toggle row, reading right to left.
    EffectIconRow row;
    row.size = kEffectIconSize;
    row.step = kEffectIconSize + 2;
    if (tsl) {
        // Clear of the vitality bar authored left of the portrait.
        row.left = mainPortrait.left - 30 - kEffectIconSize;
        row.top = mainPortrait.top + 16;
        row.step = -row.step;
    } else {
        row.left = mainPortrait.left + mainPortrait.width + 1;
        row.top = mainPortrait.top - kEffectIconSize - 4;
    }
    return row;
}

void HUD::addEffectIcons() {
    if (!_controls.LBL_BACK1) {
        return;
    }
    auto row = effectIconRowFor(_controls.LBL_BACK1->authoredExtent(), isTSL());
    for (int i = 0; i < kEffectIconCount; ++i) {
        auto label = std::static_pointer_cast<Label>(std::shared_ptr<Control>(
            _gui->newControl(ControlType::Label, "LBL_EFFECTICON" + std::to_string(i))));
        label->setBorder(Control::Border());
        label->setSelectable(false);
        label->setSharpenBorderFillAlpha(true);
        Control::Extent extent {row.left + i * row.step, row.top, row.size, row.size};
        label->setAuthoredExtent(extent);
        label->setExtent(extent);
        label->setVisible(false);
        _gui->addControlToBack(label, IGUI::ControlCoordinates::Authored);
        _effectIcons.push_back(std::move(label));
    }
}

HUD::MemberStatus HUD::memberStatus(resource::ITwoDAs &twoDas, const Creature &member) {
    MemberStatus status;
    status.down = member.isDead() || member.currentHitPoints() <= 0;
    // Cannot act: stunned, paralysed, asleep, held, choked (a knockdown
    // passes and is not shown).
    bool held = member.isDebilitated() || member.isChoked();
    for (auto type : {EffectType::Paralyze, EffectType::CutSceneParalyze, EffectType::CutSceneStunned,
                      EffectType::Sleep, EffectType::DroidStun, EffectType::Horrified,
                      EffectType::CutSceneHorrified, EffectType::Crush, EffectType::WhirlWind}) {
        held = held || member.hasEffect(type);
    }
    status.debilitated = !status.down && held;
    status.icons = creatureEffectIcons(twoDas, member);
    for (const auto &icon : status.icons) {
        (icon.good ? status.helped : status.hindered) = true;
    }
    return status;
}

void HUD::updatePartyPresentation() {
    Party &party = _game.party();
    const std::array<Label *, 3> charLabels {
        _controls.LBL_CHAR1.get(),
        _controls.LBL_CHAR2.get(),
        _controls.LBL_CHAR3.get()};
    const std::array<Label *, 3> backLabels {
        _controls.LBL_BACK1.get(),
        _controls.LBL_BACK2.get(),
        _controls.LBL_BACK3.get()};
    // K1 layouts only; null in a K2 layout.
    const std::array<Label *, 3> lvlUpBgLabels {
        _controls.LBL_LVLUPBG1.get(),
        _controls.LBL_LVLUPBG2.get(),
        _controls.LBL_LVLUPBG3.get()};
    const std::array<Label *, 3> levelUpLabels {
        _controls.LBL_LEVELUP1.get(),
        _controls.LBL_LEVELUP2.get(),
        _controls.LBL_LEVELUP3.get()};
    const std::array<ProgressBar *, 3> vitalityBars {
        _controls.PB_VIT1.get(),
        _controls.PB_VIT2.get(),
        _controls.PB_VIT3.get()};
    const std::array<ProgressBar *, 3> forceBars {
        _controls.PB_FORCE1.get(),
        _controls.PB_FORCE2.get(),
        _controls.PB_FORCE3.get()};
    auto show = [](Control *control, bool visible) {
        if (control) {
            control->setVisible(visible);
        }
    };

    const std::array<Label *, 3> debilitatedLabels {
        _controls.LBL_DEBILATATED1.get(), _controls.LBL_DEBILATATED2.get(), _controls.LBL_DEBILATATED3.get()};
    const std::array<Label *, 3> disabledLabels {
        _controls.LBL_DISABLE1.get(), _controls.LBL_DISABLE2.get(), _controls.LBL_DISABLE3.get()};
    const std::array<Label *, 3> helpedLabels {
        _controls.LBL_CMBTEFCTINC1.get(), _controls.LBL_CMBTEFCTINC2.get(), _controls.LBL_CMBTEFCTINC3.get()};
    const std::array<Label *, 3> hinderedLabels {
        _controls.LBL_CMBTEFCTRED1.get(), _controls.LBL_CMBTEFCTRED2.get(), _controls.LBL_CMBTEFCTRED3.get()};

    for (int i = 0; i < 3; ++i) {
        std::shared_ptr<Creature> member(party.getMember(i));
        // Portrait status (both layouts author these over each portrait):
        // down, cannot act, and whether good / bad effects are on.
        MemberStatus status = member ? memberStatus(_services.resource.twoDas, *member) : MemberStatus();
        show(disabledLabels[i], member && status.down);
        show(debilitatedLabels[i], member && status.debilitated);
        show(helpedLabels[i], member && status.helped);
        show(hinderedLabels[i], member && status.hindered);
        if (i == 0) {
            updateEffectIcons(member ? status.icons : std::vector<EffectIconEntry>());
        }
        show(charLabels[i], static_cast<bool>(member));
        show(backLabels[i], static_cast<bool>(member));
        show(levelUpLabels[i], member && member->isLevelUpPending());
        show(lvlUpBgLabels[i], member && member->isLevelUpPending());
        show(vitalityBars[i], static_cast<bool>(member));
        if (!member) {
            show(forceBars[i], false);
            continue;
        }
        if (charLabels[i]) {
            charLabels[i]->setBorderFill(member->portrait());
        }
        int maxHitPoints = member->maxHitPoints();
        if (vitalityBars[i]) {
            vitalityBars[i]->setValue(maxHitPoints > 0
                                          ? std::clamp(100 * member->currentHitPoints() / maxHitPoints, 0, 100)
                                          : 0);
        }
        int forcePoints = member->maxForcePoints();
        show(forceBars[i], forcePoints > 0);
        if (forceBars[i]) {
            forceBars[i]->setValue(forcePoints > 0
                                       ? std::clamp(100 * member->currentForce() / forcePoints, 0, 100)
                                       : 0);
        }
    }
}

void HUD::updateEffectIcons(const std::vector<EffectIconEntry> &icons) {
    for (size_t i = 0; i < _effectIcons.size(); ++i) {
        auto &label = _effectIcons[i];
        if (i >= icons.size()) {
            label->setVisible(false);
            continue;
        }
        if (_effectIconResRefs.size() <= i) {
            _effectIconResRefs.resize(i + 1);
        }
        if (_effectIconResRefs[i] != icons[i].icon) {
            _effectIconResRefs[i] = icons[i].icon;
            // The running game's icon art (effecticon.2da names its own).
            label->setBorderFill(_services.resource.textures.get(icons[i].icon, graphics::TextureUsage::GUI));
        }
        label->setVisible(true);
    }
}

void HUD::update(float dt) {
    _gui->update(dt);

    // Module/script work is updated before the HUD. Snapshotting here gives
    // all status events in that batch one deterministic coalescing boundary,
    // without a user-visible timer or synchronous popup from the mutation.
    if (_statusSummary && _statusSummary->presentPending()) {
        // An alignment shift in the batch plays its side's sting (guisounds
        // LightSide / DarkSide); otherwise the summary's notify sound.
        std::shared_ptr<AudioClip> clip;
        if (const auto &batch = _game.statusSummary().displayed()) {
            if (batch->entry(StatusSummaryCategory::LightSideShift).active) {
                clip = guiSound("LightSide");
            } else if (batch->entry(StatusSummaryCategory::DarkSideShift).active) {
                clip = guiSound("DarkSide");
            }
        }
        if (!clip) {
            clip = _services.resource.audioClips.get("gui_quest");
        }
        _audioSource = _services.audio.mixer.play(std::move(clip), AudioType::Sound);
    }

    updatePartyPresentation();

    auto leader = _game.party().getLeader();
    // A co-op client's creature fights on the host: its combat state is the
    // replicated one, so the combat box (Cancel Combat, clear) shows for it.
    bool inCombat = leader && leader->isInCombat();
    if (auto *coop = _game.coop(); coop && coop->isClient() && leader) {
        inCombat = inCombat || coop->clientInCombat(leader->id());
    }
    if (inCombat) {
        toggleCombat(true);
        refreshActionQueueItems();
    } else {
        toggleCombat(false);
    }
    updateCombatMessage(dt, inCombat);
    if (_k1Controls) {
        _k1Controls->update(leader.get(), leader && leader->isInCombat());
    }

    updateStatusSummaryIndicators(dt);
    updateModeToggles();

    _select.update();
    _actionBar.update();
    updateTransitionPresentation();
    _barkBubble->update(dt);
    if (_statusSummary && _statusSummary->isVisible()) {
        _statusSummary->update(dt);
    }

    // Hide minimap when there is no image to display
    setVisible(_controls.LBL_MAPBORDER, _game.map().isLoaded());

    if (_capturePresentation) {
        setVisible(_controls.LBL_CHAR1, true);
        setVisible(_controls.LBL_BACK1, true);
        for (auto [bar, value] : std::initializer_list<std::pair<ProgressBar *, int>> {
                 {_controls.PB_HEALTH.get(), 65}, {_controls.PB_VIT1.get(), 65}, {_controls.PB_FORCE1.get(), 45}}) {
            if (bar) {
                bar->setVisible(true);
                bar->setValue(value);
            }
        }
        if (_captureCombatPresentation) {
            toggleCombat(true);
            for (auto &queue : {_controls.LBL_QUEUE0, _controls.LBL_QUEUE1,
                                _controls.LBL_QUEUE2, _controls.LBL_QUEUE3}) {
                if (queue) {
                    queue->setBorderFill(g_attackIcon);
                }
            }
        }
    }
}

void HUD::showCapturePresentation(bool combat) {
    _capturePresentation = true;
    _captureCombatPresentation = combat;
    _gui->rootControl().setVisible(true);
}

void HUD::showTransitionCapturePresentation(const std::string &destination) {
    if (!_areaTransition) {
        throw std::runtime_error("Area-transition GUI is unavailable");
    }
    _captureTransitionPresentation = true;
    _areaTransition->show(destination);
}

void HUD::clearCapturePresentation() {
    _capturePresentation = false;
    _captureCombatPresentation = false;
    _captureTransitionPresentation = false;
    if (_barkBubble) {
        _barkBubble->setBarkText("", 0.0f);
    }
    if (_areaTransition) {
        _areaTransition->hide();
    }
}

void HUD::updateTransitionPresentation() {
    if (!_areaTransition) {
        return;
    }
    if (_captureTransitionPresentation) {
        return;
    }
    auto candidate = currentTransitionCandidate();
    if (candidate) {
        _areaTransition->show(candidate->destination);
    } else {
        _areaTransition->hide();
    }
}

std::optional<TransitionPortal> HUD::currentTransitionCandidate() const {
    auto module = _game.module();
    if (!module || !module->area()) {
        return std::nullopt;
    }
    auto leader = _game.party().getLeader();
    if (!leader) {
        return std::nullopt;
    }
    auto camera = _game.getActiveCamera();
    if (!camera) {
        return std::nullopt;
    }
    auto cameraSceneNode = camera->cameraSceneNode();
    if (!cameraSceneNode || !cameraSceneNode->camera()) {
        return std::nullopt;
    }
    auto &graphicsCamera = *cameraSceneNode->camera();

    TransitionView view;
    view.leaderPosition = leader->position();
    view.cameraViewProjection = graphicsCamera.projection() * graphicsCamera.view();

    return pickTransitionPortal(module->area()->transitionPresentationPortals(), view);
}

void HUD::render() {
    _gui->render();

    renderMinimap();
    renderGuestList();


    _barkBubble->render();
    if (_areaTransition && _areaTransition->isVisible()) {
        _areaTransition->render();
    }
    _select.render();
    _actionBar.render(_gui->scale());
    _game.floatingText().render();
    renderCoopNameTags();
    renderCoopChat();
}

void HUD::renderModal() {
    if (_statusSummary && _statusSummary->isVisible()) {
        _statusSummary->render();
    }
}

void HUD::renderGuestList() {
    // Co-op Free Mode (design, "HUD"): the guests as a compact list of
    // names, level and hit points, under the minimap. Text only, so it is
    // the same on the K1 and K2 layouts and carries no layout's art.
    auto *coop = _game.coop();
    if (!coop || !coop->freeMode()) {
        return;
    }
    auto guests = coop->guestList();
    if (guests.empty()) {
        return;
    }
    const auto &options = _game.options().graphics;
    std::string fontResRef(options.width > 1279 ? "fnt_d16x16b" : "fnt_d16x16a");
    if (!_guestListFont) {
        _guestListFont = _services.resource.fonts.get(fontResRef);
    }
    if (!_guestListFont) {
        return;
    }
    float scale = std::min(options.width / 800.0f, options.height / 600.0f) * options.guiScale * options.guiTextScale * 1.1f;
    float right = static_cast<float>(options.width) - 12.0f * scale;
    float top = 12.0f * scale;
    if (_controls.LBL_MAPVIEW) {
        const auto &extent = _controls.LBL_MAPVIEW->extent();
        right = static_cast<float>(_gui->controlOffset().x + extent.left + extent.width);
        top = static_cast<float>(_gui->controlOffset().y + extent.top + extent.height) + 8.0f * scale;
    }
    float lineHeight = _guestListFont->height() * scale * 1.15f;
    _services.graphics.uniforms.setGlobals([&options](auto &globals) {
        globals.reset();
        globals.projection = glm::ortho(0.0f, static_cast<float>(options.width), static_cast<float>(options.height), 0.0f, 0.0f, 100.0f);
        globals.projectionInv = glm::inverse(globals.projection);
    });
    _services.graphics.context.withBlendMode(graphics::BlendMode::Normal, [&]() {
        float y = top;
        for (const auto &guest : guests) {
            float share = guest.maxHp > 0 ? std::clamp(static_cast<float>(guest.hp) / guest.maxHp, 0.0f, 1.0f) : 0.0f;
            // Green when well, yellow when hurt, red when near death; grey while away.
            glm::vec3 color = !guest.present ? glm::vec3(0.6f) :
                              share > 0.5f  ? glm::vec3(0.55f, 0.95f, 0.55f) :
                              share > 0.25f ? glm::vec3(0.95f, 0.85f, 0.4f) :
                                              glm::vec3(0.95f, 0.4f, 0.35f);
            std::string line = guest.name + "  Lv " + std::to_string(guest.level) + "  " + std::to_string(std::max(0, static_cast<int>(guest.hp))) +
                               "/" + std::to_string(guest.maxHp);
            _guestListFont->render(line, glm::vec3(right, y, 0.0f), color, graphics::TextGravity::LeftCenter, scale);
            y += lineHeight;
        }
    });
}

void HUD::addK1StealthToggle() {
    if (isTSL() || _controls.TB_STEALTH || !_controls.TB_PAUSE || !_controls.TB_SOLO) {
        return;
    }
    // K1's mipc2* layouts author TB_STEALTH right of TB_SOLO (i_mistealth,
    // 37,500 in 800x600); its mipc* layouts have none. Those get one in the
    // same place and art, in authored coordinates.
    auto control = std::shared_ptr<Control>(_gui->newControl(ControlType::ToggleButton, "TB_STEALTH"));
    auto toggle = std::static_pointer_cast<ToggleButton>(control);
    if (auto border = _controls.TB_SOLO->borderOrNull()) {
        toggle->setBorder(*border);
    }
    if (auto hilight = _controls.TB_SOLO->hilightOrNull()) {
        toggle->setHilight(*hilight);
    }
    auto texture = [this](const char *name) {
        return _services.resource.textures.get(name, graphics::TextureUsage::GUI);
    };
    toggle->setCheckedFills(texture("i_mistealth"), texture("i_mistealth1"), texture("i_mistealth3"), texture("i_mistealth2"));
    static constexpr int kK1StealthOffset = 31; // TB_STEALTH.left - TB_SOLO.left in K1's mipc28x6
    auto extent = _controls.TB_SOLO->authoredExtent();
    extent.left += kK1StealthOffset;
    toggle->setAuthoredExtent(extent);
    toggle->setExtent(extent);
    _gui->addControlToFront(toggle, IGUI::ControlCoordinates::Authored);
    _controls.TB_STEALTH = std::move(toggle);
}

bool HUD::stealthToggleAvailable() const {
    auto leader = _game.party().getLeader();
    if (!leader) {
        return false;
    }
    // A co-op client asks the host, which knows its equipment.
    auto *coop = _game.coop();
    if (coop && coop->isClient()) {
        return true;
    }
    // Shown while a stealth unit is worn (the tutorial: "click on the Stealth button").
    return leader->isStealthed() || leader->hasStealthUnitEquipped();
}

bool HUD::soloToggleAvailable() const {
    // Co-op: the party's follow mode is the host's (as the V key).
    auto *coop = _game.coop();
    return !(coop && coop->isClient());
}

static void syncToggle(const std::shared_ptr<ToggleButton> &toggle, bool visible, bool on) {
    if (!toggle) {
        return;
    }
    toggle->setVisible(visible);
    if (toggle->isOn() != on) {
        toggle->toggle();
    }
}

void HUD::updateModeToggles() {
    const bool present = _game.party().getLeader() != nullptr;
    syncToggle(_controls.TB_PAUSE, present, _game.isPaused());
    syncToggle(_controls.TB_SOLO, present && soloToggleAvailable(), _game.party().isSoloMode());
    auto leader = _game.party().getLeader();
    syncToggle(_controls.TB_STEALTH, present && stealthToggleAvailable(), leader && leader->isStealthed());
    if (!isTSL()) {
        // K1's left moulding frames the toggle column and its backing.
        setVisible(_controls.LBL_MOULDING1, present);
        setVisible(_controls.LBL_MOULDING2, present);
    }
}

void HUD::renderMinimap() {
    if (!_controls.LBL_MAPVIEW || !_game.module()) {
        return;
    }
    const Control::Extent &extent = _controls.LBL_MAPVIEW->extent();

    glm::vec4 bounds;
    bounds[0] = static_cast<float>(_gui->controlOffset().x + extent.left);
    bounds[1] = static_cast<float>(_gui->controlOffset().y + extent.top);
    bounds[2] = static_cast<float>(extent.width);
    bounds[3] = static_cast<float>(extent.height);

    // The frame is a control and scales with the layout; the map drawn inside
    // it is not, so it has to be told.
    _game.map().render(Map::Mode::Minimap, bounds, _gui->scale());
}

void HUD::toggleCombat(bool enabled) {
    // The auto-combat toggle is usable out of combat too (owner, playtest 3).
    refreshAutoCombatButton(_game.party().getLeader() != nullptr);
    for (auto control : std::initializer_list<std::shared_ptr<Control>> {
             _controls.BTN_CLEARALL, _controls.BTN_CLEARONE, _controls.BTN_CLEARONE2,
             _controls.LBL_CMBTMODEMSG, _controls.LBL_CMBTMSGBG,
             _controls.LBL_QUEUE0, _controls.LBL_QUEUE1, _controls.LBL_QUEUE2, _controls.LBL_QUEUE3,
             _controls.LBL_COMBATBG3}) {
        setVisible(control, enabled);
    }
    if (!isTSL()) {
        // K1's backings all follow combat. The first two TSL backings are
        // persistent HUD art (and are absent from a K2 layout); the third is
        // the combat-sequence extension.
        setVisible(_controls.LBL_COMBATBG1, enabled);
        setVisible(_controls.LBL_COMBATBG2, enabled);
    }
}

// TLK: the combat box's messages (K2 behaviour, design note p2_ui).
static constexpr int kCombatModeEngagedStrRef = 111303;
static constexpr int kCombatModeActiveStrRef = 37235; // K1's TLK has no 111303 text
static constexpr int kClosingToAttackRangeStrRef = 42477;
static constexpr int kPlayerMovingStrRef = 42476;
static constexpr float kCombatMessageHold = 2.5f;

void HUD::updateCombatMessage(float dt, bool inCombat) {
    if (!_controls.LBL_CMBTMODEMSG) {
        return;
    }
    _combatMessageHold = std::max(0.0f, _combatMessageHold - dt);
    if (!inCombat) {
        if (_combatMessageStrRef != -1) {
            _combatMessageStrRef = -1;
            _combatMessageHold = 0.0f;
            _controls.LBL_CMBTMODEMSG->setTextMessage("");
        }
        return;
    }
    // The leader's current combat state: closing in on its attack target,
    // else plain combat mode.
    int strRef = kCombatModeEngagedStrRef;
    if (auto leader = _game.party().getLeader()) {
        const auto &actions = leader->actions();
        if (!actions.empty() && actions.front()->type() == ActionType::AttackObject) {
            auto attack = std::static_pointer_cast<AttackObjectAction>(actions.front());
            if (attack->closingIn()) {
                strRef = kClosingToAttackRangeStrRef;
            }
        } else if (!actions.empty() && actions.front()->type() == ActionType::MoveToPoint &&
                   actions.front()->isUserAction()) {
            // The player walks the leader off mid-fight.
            strRef = kPlayerMovingStrRef;
        }
    }
    if (strRef == _combatMessageStrRef) {
        return;
    }
    // "Closing to attack range." holds the box for 2.5 s against the next
    // message; combat mode and "Player moving" do not hold.
    if (_combatMessageHold > 0.0f) {
        return;
    }
    _combatMessageStrRef = strRef;
    if (strRef == kClosingToAttackRangeStrRef) {
        _combatMessageHold = kCombatMessageHold;
    }
    auto &strings = _services.resource.strings;
    std::string text = strings.getText(strRef);
    if (text.empty() && strRef == kCombatModeEngagedStrRef) {
        text = strings.getText(kCombatModeActiveStrRef);
    }
    _controls.LBL_CMBTMODEMSG->setTextMessage(text);
}

void HUD::refreshActionQueueItems() const {
    auto leader = _game.party().getLeader();
    if (!leader) {
        return;
    }
    const std::array<Label *, 4> queueLabels {
        _controls.LBL_QUEUE0.get(),
        _controls.LBL_QUEUE1.get(),
        _controls.LBL_QUEUE2.get(),
        _controls.LBL_QUEUE3.get()};

    // A co-op client's character fights on the host: its queue is the
    // replicated one. Otherwise the leader's own actions, where a talent
    // the party AI queued shows as the feat or power it runs.
    std::vector<net::QueueEntry> entries;
    std::vector<std::shared_ptr<Action>> actions; // the leader's own, for an item's icon
    if (auto *coop = _game.coop(); coop && coop->isClient()) {
        entries = coop->clientQueue(leader->id());
    } else {
        entries = combatqueue::entriesOf(*leader);
        actions.assign(leader->actions().begin(), leader->actions().end());
    }

    for (size_t i = 0; i < queueLabels.size(); ++i) {
        if (!queueLabels[i]) {
            continue;
        }
        Label &item = *queueLabels[i];
        // A slot with nothing of its own to show (a walk, or a feat without
        // art) must not keep the icon of the order that stood there before.
        item.setBorderFill("");
        if (i >= entries.size()) {
            continue;
        }
        const net::QueueEntry &entry = entries[i];
        switch (entry.kind) {
        case net::kQueueAttack:
            item.setBorderFill(g_attackIcon);
            break;
        case net::kQueueSwap:
            // A queued weapon switch shows in the combat queue. K1 has no
            // switch icon; the K1 HUD uses its swap button's K1 art.
            item.setBorderFill(isTSL() ? g_switchWeaponsIcon : std::string(K1HUDControls::kQueuedSwapIcon));
            break;
        case net::kQueueFeat: {
            std::shared_ptr<Feat> feat(_services.game.feats.get(static_cast<FeatType>(entry.param)));
            if (feat && feat->icon) {
                item.setBorderFill(feat->icon);
            } else {
                item.setBorderFill(g_attackIcon); // a feat attack is an attack
            }
            break;
        }
        case net::kQueueSpell: {
            std::shared_ptr<Spell> spell(_services.game.spells.get(static_cast<SpellType>(entry.param)));
            std::shared_ptr<Action> own = i < actions.size() ? actions[i] : nullptr;
            if (spell && spell->icon) {
                item.setBorderFill(spell->icon);
            } else if (auto cast = own ? dyn_cast<CastSpellAtObjectAction>(own.get()) : nullptr; cast && cast->item()) {
                item.setBorderFill(cast->item().value()->icon());
            }
            break;
        }
        default:
            break;
        }
    }
}

} // namespace game

} // namespace reone
