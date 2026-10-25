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

#pragma once

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/progressbar.h"
#include "reone/gui/control/togglebutton.h"
#include "reone/resource/types.h"

#include "../creaturestates.h"
#include "../gui.h"
#include "../transitioncandidate.h"

#include "actionbar.h"
#include "areatransition.h"
#include "barkbubble.h"
#include "k1hudcontrols.h"
#include "selectoverlay.h"
#include "statussummary.h"

namespace reone {

namespace audio {

class AudioClip;

}

} // namespace reone

namespace reone {

namespace game {

class Creature;

class HUD : public GameGUI {
    friend class TestGameModule;

public:
    HUD(Game &game, ServicesView &services) :
        GameGUI(game, services, PresentationFamily::Hud),
        _select(game, services),
        _actionBar(game, services),
        _areaTransition(nullptr) {
        _resRef = guiResRef("mipc28x6");
    }

    bool handle(const input::Event &event) override;
    void update(float dt) override;
    void render() override;
    void renderModal();

    BarkBubble &barkBubble() const { return *_barkBubble; }

    /** Keymap 1-3: act with the target's left / middle / right action. */
    void activateTargetAction(int index);
    /** Keymap 4-7: act with a personal action slot (power, medical, other, mines). */
    void activatePersonalAction(int index);

    void activateStatusSummaryIndicator(StatusSummaryCategory category);
    /** Acknowledges a visible status summary as its OK button does. */
    bool dismissStatusSummary() {
        if (!_statusSummary || !_statusSummary->isVisible()) {
            return false;
        }
        _statusSummary->dismiss();
        return true;
    }
    void resetStatusSummaryPresentation();

    /** Exposes representative HUD state for the scripted gallery fixture. */
    void showCapturePresentation(bool combat);
    /** Exposes the area-transition banner for the scripted gallery fixture. */
    void showTransitionCapturePresentation(const std::string &destination);
    /** Clears capture-only HUD and bark state before the next gallery scene. */
    void clearCapturePresentation();

    /**
     * The tags a HUD layout is expected to author, for a K2 (tsl) or K1
     * layout. The HUD tolerates any of them missing.
     */
    static std::vector<std::string> expectedTags(bool tsl);

private:
    friend class HUDTestAccess;
    friend class HUDTagTestAccess;
    friend class K1HUDTestAccess;

    struct Controls {
        std::shared_ptr<gui::Button> BTN_ABI;
        std::shared_ptr<gui::Button> BTN_CHAR;
        std::shared_ptr<gui::Button> BTN_CHAR1;
        std::shared_ptr<gui::Button> BTN_CHAR2;
        std::shared_ptr<gui::Button> BTN_CHAR3;
        std::shared_ptr<gui::Button> BTN_CLEARALL;
        std::shared_ptr<gui::Button> BTN_CLEARONE;
        std::shared_ptr<gui::Button> BTN_CLEARONE2;
        std::shared_ptr<gui::Button> BTN_EQU;
        std::shared_ptr<gui::Button> BTN_INV;
        std::shared_ptr<gui::Button> BTN_JOU;
        std::shared_ptr<gui::Button> BTN_MAP;
        std::shared_ptr<gui::Button> BTN_MINIMAP;
        std::shared_ptr<gui::Button> BTN_MSG;
        std::shared_ptr<gui::Button> BTN_OPT;
        std::shared_ptr<gui::ToggleButton> BTN_SWAPWEAPONS;
        std::shared_ptr<gui::Button> BTN_TARGET0;
        std::shared_ptr<gui::Button> BTN_TARGET1;
        std::shared_ptr<gui::Button> BTN_TARGET2;
        std::shared_ptr<gui::Button> BTN_TARGETDOWN0;
        std::shared_ptr<gui::Button> BTN_TARGETDOWN1;
        std::shared_ptr<gui::Button> BTN_TARGETDOWN2;
        std::shared_ptr<gui::Button> BTN_TARGETUP0;
        std::shared_ptr<gui::Button> BTN_TARGETUP1;
        std::shared_ptr<gui::Button> BTN_TARGETUP2;
        std::shared_ptr<gui::Label> LBL_ARROW;
        std::shared_ptr<gui::Label> LBL_ARROW_MARGIN;
        std::shared_ptr<gui::Label> LBL_BACK1;
        std::shared_ptr<gui::Label> LBL_BACK2;
        std::shared_ptr<gui::Label> LBL_BACK3;
        std::shared_ptr<gui::Label> LBL_CASH;
        std::shared_ptr<gui::Label> LBL_CHAR1;
        std::shared_ptr<gui::Label> LBL_CHAR2;
        std::shared_ptr<gui::Label> LBL_CHAR3;
        std::shared_ptr<gui::Label> LBL_CMBTEFCTINC1;
        std::shared_ptr<gui::Label> LBL_CMBTEFCTINC2;
        std::shared_ptr<gui::Label> LBL_CMBTEFCTINC3;
        std::shared_ptr<gui::Label> LBL_CMBTEFCTRED1;
        std::shared_ptr<gui::Label> LBL_CMBTEFCTRED2;
        std::shared_ptr<gui::Label> LBL_CMBTEFCTRED3;
        std::shared_ptr<gui::Label> LBL_CMBTMODEMSG;
        std::shared_ptr<gui::Label> LBL_CMBTMSGBG;
        std::shared_ptr<gui::Label> LBL_COMBATBG1;
        std::shared_ptr<gui::Label> LBL_COMBATBG2;
        std::shared_ptr<gui::Label> LBL_COMBATBG3;
        std::shared_ptr<gui::Label> LBL_DARKSHIFT;
        std::shared_ptr<gui::Label> LBL_DEBILATATED1;
        std::shared_ptr<gui::Label> LBL_DEBILATATED2;
        std::shared_ptr<gui::Label> LBL_DEBILATATED3;
        std::shared_ptr<gui::Label> LBL_DISABLE1;
        std::shared_ptr<gui::Label> LBL_DISABLE2;
        std::shared_ptr<gui::Label> LBL_DISABLE3;
        std::shared_ptr<gui::Label> LBL_HEALTHBG;
        std::shared_ptr<gui::Label> LBL_ITEMLOST;
        std::shared_ptr<gui::Label> LBL_ITEMRCVD;
        std::shared_ptr<gui::Label> LBL_JOURNAL;
        std::shared_ptr<gui::Label> LBL_LEVELUP1;
        std::shared_ptr<gui::Label> LBL_LEVELUP2;
        std::shared_ptr<gui::Label> LBL_LEVELUP3;
        std::shared_ptr<gui::Label> LBL_LIGHTSHIFT;
        std::shared_ptr<gui::Label> LBL_LVLUPBG1;
        std::shared_ptr<gui::Label> LBL_LVLUPBG2;
        std::shared_ptr<gui::Label> LBL_LVLUPBG3;
        std::shared_ptr<gui::Label> LBL_MAP;
        std::shared_ptr<gui::Label> LBL_MAPBORDER;
        std::shared_ptr<gui::Label> LBL_MAPVIEW;
        std::shared_ptr<gui::Label> LBL_MENUBG;
        std::shared_ptr<gui::Label> LBL_MOULDING1;
        std::shared_ptr<gui::Label> LBL_MOULDING2;
        std::shared_ptr<gui::Label> LBL_MOULDING3;
        std::shared_ptr<gui::Label> LBL_NAME;
        std::shared_ptr<gui::Label> LBL_NAMEBG;
        std::shared_ptr<gui::Label> LBL_PLOTXP;
        std::shared_ptr<gui::Label> LBL_QUEUE0;
        std::shared_ptr<gui::Label> LBL_QUEUE1;
        std::shared_ptr<gui::Label> LBL_QUEUE2;
        std::shared_ptr<gui::Label> LBL_QUEUE3;
        std::shared_ptr<gui::Label> LBL_STEALTHXP;
        std::shared_ptr<gui::Button> LBL_TARGET0;
        std::shared_ptr<gui::Button> LBL_TARGET1;
        std::shared_ptr<gui::Button> LBL_TARGET2;
        std::shared_ptr<gui::ProgressBar> PB_FORCE1;
        std::shared_ptr<gui::ProgressBar> PB_FORCE2;
        std::shared_ptr<gui::ProgressBar> PB_FORCE3;
        std::shared_ptr<gui::ProgressBar> PB_HEALTH;
        std::shared_ptr<gui::ProgressBar> PB_VIT1;
        std::shared_ptr<gui::ProgressBar> PB_VIT2;
        std::shared_ptr<gui::ProgressBar> PB_VIT3;
        std::shared_ptr<gui::ToggleButton> TB_PAUSE;
        std::shared_ptr<gui::ToggleButton> TB_SOLO;
        std::shared_ptr<gui::ToggleButton> TB_STEALTH;
    };

    Controls _controls;

    SelectionOverlay _select;
    ActionBar _actionBar;
    std::unique_ptr<BarkBubble> _barkBubble;
    std::unique_ptr<StatusSummary> _statusSummary;
    std::unique_ptr<AreaTransition> _areaTransition;
    // One HUD flash per status-summary category (LBL_JOURNAL, LBL_CASH,
    // LBL_PLOTXP, LBL_STEALTHXP, LBL_DARKSHIFT, LBL_LIGHTSHIFT, LBL_ITEMRCVD,
    // LBL_ITEMLOST), in category order.
    std::array<StatusSummaryIndicator, kStatusSummaryCategoryCount> _indicators;
    std::array<gui::Label *, kStatusSummaryCategoryCount> indicatorLabels() const;
    void updateStatusSummaryIndicators(float dt);
    std::shared_ptr<audio::AudioClip> guiSound(const std::string &label) const;

    // The combat box's message (LBL_CMBTMODEMSG), design note p2_ui.
    int _combatMessageStrRef {-1};
    float _combatMessageHold {0.0f};
    void updateCombatMessage(float dt, bool inCombat);
    bool _capturePresentation {false};
    bool _captureCombatPresentation {false};
    bool _captureTransitionPresentation {false};

    void preload(gui::IGUI &gui) override;
    void onGUILoaded() override;

    void bindControls() {
        _controls.BTN_ABI = findControl<gui::Button>("BTN_ABI");
        _controls.BTN_CHAR = findControl<gui::Button>("BTN_CHAR");
        _controls.BTN_CHAR1 = findControl<gui::Button>("BTN_CHAR1");
        _controls.BTN_CHAR2 = findControl<gui::Button>("BTN_CHAR2");
        _controls.BTN_CHAR3 = findControl<gui::Button>("BTN_CHAR3");
        _controls.BTN_CLEARALL = findControl<gui::Button>("BTN_CLEARALL");
        _controls.BTN_CLEARONE = findControl<gui::Button>("BTN_CLEARONE");
        _controls.BTN_CLEARONE2 = findControl<gui::Button>("BTN_CLEARONE2");
        _controls.BTN_EQU = findControl<gui::Button>("BTN_EQU");
        _controls.BTN_INV = findControl<gui::Button>("BTN_INV");
        _controls.BTN_JOU = findControl<gui::Button>("BTN_JOU");
        _controls.BTN_MAP = findControl<gui::Button>("BTN_MAP");
        _controls.BTN_MINIMAP = findControl<gui::Button>("BTN_MINIMAP");
        _controls.BTN_MSG = findControl<gui::Button>("BTN_MSG");
        _controls.BTN_OPT = findControl<gui::Button>("BTN_OPT");
        _controls.BTN_SWAPWEAPONS = findControl<gui::ToggleButton>("BTN_SWAPWEAPONS");
        _controls.BTN_TARGET0 = findControl<gui::Button>("BTN_TARGET0");
        _controls.BTN_TARGET1 = findControl<gui::Button>("BTN_TARGET1");
        _controls.BTN_TARGET2 = findControl<gui::Button>("BTN_TARGET2");
        _controls.BTN_TARGETDOWN0 = findControl<gui::Button>("BTN_TARGETDOWN0");
        _controls.BTN_TARGETDOWN1 = findControl<gui::Button>("BTN_TARGETDOWN1");
        _controls.BTN_TARGETDOWN2 = findControl<gui::Button>("BTN_TARGETDOWN2");
        _controls.BTN_TARGETUP0 = findControl<gui::Button>("BTN_TARGETUP0");
        _controls.BTN_TARGETUP1 = findControl<gui::Button>("BTN_TARGETUP1");
        _controls.BTN_TARGETUP2 = findControl<gui::Button>("BTN_TARGETUP2");
        _controls.LBL_ARROW = findControl<gui::Label>("LBL_ARROW");
        _controls.LBL_ARROW_MARGIN = findControl<gui::Label>("LBL_ARROW_MARGIN");
        _controls.LBL_BACK1 = findControl<gui::Label>("LBL_BACK1");
        _controls.LBL_BACK2 = findControl<gui::Label>("LBL_BACK2");
        _controls.LBL_BACK3 = findControl<gui::Label>("LBL_BACK3");
        _controls.LBL_CASH = findControl<gui::Label>("LBL_CASH");
        _controls.LBL_CHAR1 = findControl<gui::Label>("LBL_CHAR1");
        _controls.LBL_CHAR2 = findControl<gui::Label>("LBL_CHAR2");
        _controls.LBL_CHAR3 = findControl<gui::Label>("LBL_CHAR3");
        _controls.LBL_CMBTEFCTINC1 = findControl<gui::Label>("LBL_CMBTEFCTINC1");
        _controls.LBL_CMBTEFCTINC2 = findControl<gui::Label>("LBL_CMBTEFCTINC2");
        _controls.LBL_CMBTEFCTINC3 = findControl<gui::Label>("LBL_CMBTEFCTINC3");
        _controls.LBL_CMBTEFCTRED1 = findControl<gui::Label>("LBL_CMBTEFCTRED1");
        _controls.LBL_CMBTEFCTRED2 = findControl<gui::Label>("LBL_CMBTEFCTRED2");
        _controls.LBL_CMBTEFCTRED3 = findControl<gui::Label>("LBL_CMBTEFCTRED3");
        _controls.LBL_CMBTMODEMSG = findControl<gui::Label>("LBL_CMBTMODEMSG");
        _controls.LBL_CMBTMSGBG = findControl<gui::Label>("LBL_CMBTMSGBG");
        _controls.LBL_COMBATBG1 = findControl<gui::Label>("LBL_COMBATBG1");
        _controls.LBL_COMBATBG2 = findControl<gui::Label>("LBL_COMBATBG2");
        _controls.LBL_COMBATBG3 = findControl<gui::Label>("LBL_COMBATBG3");
        _controls.LBL_DARKSHIFT = findControl<gui::Label>("LBL_DARKSHIFT");
        _controls.LBL_DEBILATATED1 = findControl<gui::Label>("LBL_DEBILATATED1");
        _controls.LBL_DEBILATATED2 = findControl<gui::Label>("LBL_DEBILATATED2");
        _controls.LBL_DEBILATATED3 = findControl<gui::Label>("LBL_DEBILATATED3");
        _controls.LBL_DISABLE1 = findControl<gui::Label>("LBL_DISABLE1");
        _controls.LBL_DISABLE2 = findControl<gui::Label>("LBL_DISABLE2");
        _controls.LBL_DISABLE3 = findControl<gui::Label>("LBL_DISABLE3");
        _controls.LBL_HEALTHBG = findControl<gui::Label>("LBL_HEALTHBG");
        _controls.LBL_ITEMLOST = findControl<gui::Label>("LBL_ITEMLOST");
        _controls.LBL_ITEMRCVD = findControl<gui::Label>("LBL_ITEMRCVD");
        _controls.LBL_JOURNAL = findControl<gui::Label>("LBL_JOURNAL");
        _controls.LBL_LEVELUP1 = findControl<gui::Label>("LBL_LEVELUP1");
        _controls.LBL_LEVELUP2 = findControl<gui::Label>("LBL_LEVELUP2");
        _controls.LBL_LEVELUP3 = findControl<gui::Label>("LBL_LEVELUP3");
        _controls.LBL_LIGHTSHIFT = findControl<gui::Label>("LBL_LIGHTSHIFT");
        _controls.LBL_LVLUPBG1 = findControl<gui::Label>("LBL_LVLUPBG1");
        _controls.LBL_LVLUPBG2 = findControl<gui::Label>("LBL_LVLUPBG2");
        _controls.LBL_LVLUPBG3 = findControl<gui::Label>("LBL_LVLUPBG3");
        _controls.LBL_MAP = findControl<gui::Label>("LBL_MAP");
        _controls.LBL_MAPBORDER = findControl<gui::Label>("LBL_MAPBORDER");
        _controls.LBL_MAPVIEW = findControl<gui::Label>("LBL_MAPVIEW");
        _controls.LBL_MENUBG = findControl<gui::Label>("LBL_MENUBG");
        _controls.LBL_MOULDING1 = findControl<gui::Label>("LBL_MOULDING1");
        _controls.LBL_MOULDING2 = findControl<gui::Label>("LBL_MOULDING2");
        _controls.LBL_MOULDING3 = findControl<gui::Label>("LBL_MOULDING3");
        _controls.LBL_NAME = findControl<gui::Label>("LBL_NAME");
        _controls.LBL_NAMEBG = findControl<gui::Label>("LBL_NAMEBG");
        _controls.LBL_PLOTXP = findControl<gui::Label>("LBL_PLOTXP");
        _controls.LBL_QUEUE0 = findControl<gui::Label>("LBL_QUEUE0");
        _controls.LBL_QUEUE1 = findControl<gui::Label>("LBL_QUEUE1");
        _controls.LBL_QUEUE2 = findControl<gui::Label>("LBL_QUEUE2");
        _controls.LBL_QUEUE3 = findControl<gui::Label>("LBL_QUEUE3");
        _controls.LBL_STEALTHXP = findControl<gui::Label>("LBL_STEALTHXP");
        _controls.LBL_TARGET0 = findControl<gui::Button>("LBL_TARGET0");
        _controls.LBL_TARGET1 = findControl<gui::Button>("LBL_TARGET1");
        _controls.LBL_TARGET2 = findControl<gui::Button>("LBL_TARGET2");
        _controls.PB_FORCE1 = findControl<gui::ProgressBar>("PB_FORCE1");
        _controls.PB_FORCE2 = findControl<gui::ProgressBar>("PB_FORCE2");
        _controls.PB_FORCE3 = findControl<gui::ProgressBar>("PB_FORCE3");
        _controls.PB_HEALTH = findControl<gui::ProgressBar>("PB_HEALTH");
        _controls.PB_VIT1 = findControl<gui::ProgressBar>("PB_VIT1");
        _controls.PB_VIT2 = findControl<gui::ProgressBar>("PB_VIT2");
        _controls.PB_VIT3 = findControl<gui::ProgressBar>("PB_VIT3");
        _controls.TB_PAUSE = findControl<gui::ToggleButton>("TB_PAUSE");
        _controls.TB_SOLO = findControl<gui::ToggleButton>("TB_SOLO");
        _controls.TB_STEALTH = findControl<gui::ToggleButton>("TB_STEALTH");
    }

    void updatePartyPresentation();
    void toggleCombat(bool enabled);
public:
    /** The leader's effect-icon row, in authored space. step < 0: right to left. */
    struct EffectIconRow {
        int left {0};
        int top {0};
        int size {0};
        int step {0};
    };
    static constexpr int kEffectIconCount = 6;
    static constexpr int kEffectIconSize = 20;
    static EffectIconRow effectIconRowFor(const gui::Control::Extent &mainPortrait, bool tsl);

    /** What a party member's portrait indicators show. */
    struct MemberStatus {
        bool down {false};        // LBL_DISABLE: dead or at 0 hit points
        bool debilitated {false}; // LBL_DEBILATATED: cannot act
        bool helped {false};      // LBL_CMBTEFCTINC: a good effect icon is on
        bool hindered {false};    // LBL_CMBTEFCTRED: a bad effect icon is on
        std::vector<EffectIconEntry> icons;
    };
    static MemberStatus memberStatus(resource::ITwoDAs &twoDas, const Creature &member);

private:
    std::vector<std::shared_ptr<gui::Label>> _effectIcons;
    std::vector<std::string> _effectIconResRefs;
    void addEffectIcons();
    void updateEffectIcons(const std::vector<EffectIconEntry> &icons);

    // TB_PAUSE / TB_SOLO follow the game's state each frame (design note k1hud_parity_2).
    void updateModeToggles();
    bool soloToggleAvailable() const;
    /** K1's mipc* layouts lack the TB_STEALTH its mipc2* ones author: one in that art and place. */
    void addK1StealthToggle();
    bool stealthToggleAvailable() const;

    // P1 item 10: the auto-combat toggle, built at load beside the weapon-swap
    // button (K2 layout) or the Cancel Combat button (K1 layout, which has no
    // weapon-swap button).
    std::shared_ptr<gui::Control> _autoCombatButton;
    // K1 HUD parity (design note k1hud_parity): weapon-set swap, set indicator and the
    // K1-art auto-combat toggle. Built on the K1 layout only.
    std::unique_ptr<K1HUDControls> _k1Controls;
    void addAutoCombatButton();
    void refreshAutoCombatButton(bool visible);
    void refreshActionQueueItems() const;
    void updateTransitionPresentation();
    std::optional<TransitionPortal> currentTransitionCandidate() const;

    void renderMinimap();
    void renderGuestList();
    // Co-op chat overlay and ping markers (hudchat.cpp).
    void renderCoopChat();
    float chatOverlayBottom() const;
    std::shared_ptr<graphics::Font> _guestListFont;
    // Co-op name tags and guest-character portraits (hudcoop.cpp).
    struct CoopPortrait {
        std::shared_ptr<Creature> creature;
        glm::vec4 rect; // x, y, width, height in screen pixels
    };
    std::vector<CoopPortrait> _coopPortraits;
    bool coopOverlaysShown() const;
    void renderCoopNameTags();
    struct PortraitSource {
        std::shared_ptr<Creature> creature;
        std::string name;
        uint8_t colour {0};
    };
    void renderCoopPortraits(const std::vector<PortraitSource> &sources, float scale);
    bool handleCoopPortraitClick(const input::Event &event);
};

} // namespace game

} // namespace reone
