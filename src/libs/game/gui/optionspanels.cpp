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

#include "reone/game/gui/optionspanels.h"

#include "reone/gui/control/button.h"
#include "reone/gui/control/listbox.h"
#include "reone/gui/control/slider.h"
#include "reone/gui/control/togglebutton.h"
#include "reone/system/logutil.h"

#include "reone/game/coopsession.h"
#include "reone/game/game.h"
#include "reone/game/keymap.h"
#include "reone/game/options.h"
#include "reone/graphics/preset.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/strings.h"

#include <algorithm>

using namespace reone::gui;

namespace reone {

namespace game {

namespace {

// Buttons that leave a panel, as its Close / OK / Cancel do.
const char *const kBackTags[] {"BTN_BACK", "BTN_Accept", "BTN_OK", "BTN_Cancel", "BTN_CANCEL", "BTN_EXIT"};
const char *const kSmartCompanionsTag = "CB_SMARTCOMPANIONS";
const char *const kDefaultTags[] {"BTN_DEFAULT", "BTN_Default"};

// Volume sliders and the settings they drive (0..100).
struct SliderSetting {
    const char *tag;
    const char *key;
};
const SliderSetting kSliders[] {
    {"SLI_MUSIC", "audio.music"},
    {"SLI_VO", "audio.voice"},
    {"SLI_FX", "audio.sound"},
    {"SLI_MOVIE", "audio.movie"},
};

// The panel name without the K2 "_p" / "_x" suffix: settings are shared.
std::string panelStem(const std::string &base) {
    if (base.size() > 2 && base[base.size() - 2] == '_') {
        return base.substr(0, base.size() - 2);
    }
    return base;
}

bool isPopup(const std::string &base) {
    return base.rfind("optresolution", 0) == 0 || base.rfind("optkeyentry", 0) == 0;
}

// "Smarter companions" (C13): a check box of our own at the foot of the
// gameplay panel's column, styled as its neighbours. Both layouts are full,
// so the column makes room: K1 moves its two buttons into the space below
// them; K2, which has none, closes the gaps between its check boxes.
void addSmartCompanionsToggle(IGUI &gui, bool disabled) {
    std::vector<std::shared_ptr<Control>> column;
    for (const char *tag : {"CB_LEVELUP", "CB_INVERTCAM", "CB_AUTOSAVE", "CB_REVERSE_INGAME", "CB_REVERSE", "CB_DISABLEMOVE"}) {
        if (auto control = gui.findControl(tag)) {
            column.push_back(std::move(control));
        }
    }
    if (column.empty() || gui.findControl(kSmartCompanionsTag)) {
        return;
    }
    std::stable_sort(column.begin(), column.end(), [](const auto &a, const auto &b) {
        return a->authoredExtent().top < b->authoredExtent().top;
    });
    const Control &last = *column.back();
    int pitch = column.size() >= 2
                    ? last.authoredExtent().top - column[column.size() - 2]->authoredExtent().top
                    : last.authoredExtent().height;
    Control::Extent extent = last.authoredExtent();
    extent.top += pitch;

    auto mouse = gui.findControl("BTN_MOUSE");
    auto keymap = gui.findControl("BTN_KEYMAP");
    if (mouse && extent.top + extent.height > mouse->authoredExtent().top) {
        int need = extent.top + extent.height - mouse->authoredExtent().top;
        int bottom = mouse->authoredExtent().top + mouse->authoredExtent().height;
        if (keymap) {
            bottom = std::max(bottom, keymap->authoredExtent().top + keymap->authoredExtent().height);
        }
        int floor = std::numeric_limits<int>::max();
        for (const char *tag : {"BTN_BACK", "BTN_DEFAULT", "LBL_BAR3"}) {
            if (auto control = gui.findControl(tag); control && control->authoredExtent().top >= bottom) {
                floor = std::min(floor, control->authoredExtent().top);
            }
        }
        if (floor != std::numeric_limits<int>::max() && bottom + need <= floor) {
            for (const auto &button : {mouse, keymap}) {
                if (button) {
                    auto moved = button->authoredExtent();
                    moved.top += need;
                    button->setAuthoredExtent(moved);
                }
            }
        } else {
            int top = column.front()->authoredExtent().top;
            for (const auto &control : column) {
                auto moved = control->authoredExtent();
                moved.top = top;
                control->setAuthoredExtent(moved);
                top += moved.height;
            }
            extent.top = top;
        }
    }

    auto toggle = std::shared_ptr<Control>(gui.newControl(ControlType::ToggleButton, kSmartCompanionsTag));
    toggle->setBorder(last.border());
    toggle->setHilight(last.hilight());
    toggle->setText(last.text());
    toggle->setTextMessage("Smarter companions");
    toggle->setPadding(last.padding());
    toggle->setTintBorderFill(gui.tintBorderFills()); // as loaded controls are
    toggle->setAuthoredExtent(extent);
    toggle->setExtent(extent);
    toggle->setDisabled(disabled);
    gui.addControlToBack(toggle, IGUI::ControlCoordinates::Authored);
    gui.refreshLayout();
}

// Enhanced preset controls (design note graphics_scope, Tier 1).
const char *const kEnhancedTag = "CB_ENHANCED";
struct AddedToggle {
    const char *tag;
    const char *text;
};
const AddedToggle kGraphicsToggles[] {{kEnhancedTag, "Enhanced Graphics"}};
const AddedToggle kAdvancedToggles[] {
    {"CB_BLOOM", "Bloom"},
    {"CB_AMBIENTOCC", "Ambient Occlusion"},
    {"CB_HIRESSHADOWS", "High-Res Shadows"},
    {"CB_SHARPEN", "Sharpening"},
};
const char *const kAntiAliasingNames[] {"Off", "FXAA", "SMAA", "TAA"};
const char *const kAnisotropyNames[] {"Off", "2x", "4x", "8x", "16x"};

bool isAdvancedGraphics(const std::string &stem) {
    return stem == "optgraphicsadv" || stem == "optgraphicadv";
}

// Add check boxes of our own under a panel's column of options (check boxes
// and arrow selectors), styled as its last check box. When the column would
// run into what is below it, every row moves up to an even, closer pitch.
void addColumnToggles(IGUI &gui, const std::vector<AddedToggle> &additions) {
    static const char *const kColumnTags[] {
        "CB_SHADOWS", "CB_GRASS", "CB_FORCESPEED", "BTN_TEXQUAL", "BTN_ANTIALIAS", "BTN_ANISOTROPY",
        "CB_FRAMEBUFF", "CB_SOFTSHADOWS", "CB_VSYNC", "CB_FORCESOFTWARE"};
    static const char *const kLimitTags[] {
        "BTN_RESOLUTION", "BTN_ADVANCED", "LBL_BAR3", "LBL_BAR4", "BTN_BACK", "BTN_DEFAULT", "BTN_CANCEL"};
    std::vector<std::shared_ptr<Control>> rows;
    std::shared_ptr<Control> lastToggle;
    for (const char *tag : kColumnTags) {
        if (auto control = gui.findControl(tag)) {
            rows.push_back(control);
        }
    }
    if (rows.empty() || gui.findControl(additions.front().tag)) {
        return;
    }
    std::stable_sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
        return a->authoredExtent().top < b->authoredExtent().top;
    });
    for (auto &row : rows) {
        if (row->controlType() == ControlType::ToggleButton) {
            lastToggle = row;
        }
    }
    if (!lastToggle) {
        return;
    }
    const auto first = rows.front()->authoredExtent();
    int pitch = rows.size() >= 2
                    ? (rows.back()->authoredExtent().top - first.top) / static_cast<int>(rows.size() - 1)
                    : first.height + 4;
    int rowHeight = lastToggle->authoredExtent().height;
    int lastBottom = rows.back()->authoredExtent().top + rows.back()->authoredExtent().height;
    int limit = std::numeric_limits<int>::max();
    for (const char *tag : kLimitTags) {
        auto control = gui.findControl(tag);
        if (!control) {
            continue;
        }
        const auto &e = control->authoredExtent();
        bool overlapsColumn = e.left < first.left + first.width && e.left + e.width > first.left;
        if (!overlapsColumn) {
            continue;
        }
        if (e.top >= lastBottom) {
            limit = std::min(limit, e.top);
        } else if (e.top + e.height > lastBottom) {
            limit = std::min(limit, e.top + e.height); // a frame around the column
        }
    }
    // New rows keep the authored check-box height, so their art matches the
    // rows above; the authored rows already overlap their neighbours.
    int count = static_cast<int>(rows.size() + additions.size());
    if (limit != std::numeric_limits<int>::max() && first.top + (count - 1) * pitch + rowHeight > limit) {
        pitch = std::max(1, (limit - first.top - rowHeight) / std::max(1, count - 1));
    }
    // Lay out the column again: each row with its arrows, then the new rows.
    int top = first.top;
    for (auto &row : rows) {
        int dy = top - row->authoredExtent().top;
        for (const char *suffix : {"", "LEFT", "RIGHT"}) {
            auto control = suffix[0] == 0 ? row : gui.findControl(row->tag() + suffix);
            if (!control) {
                continue;
            }
            auto moved = control->authoredExtent();
            moved.top += dy;
            control->setAuthoredExtent(moved);
        }
        top += pitch;
    }
    for (const auto &addition : additions) {
        Control::Extent extent = lastToggle->authoredExtent();
        extent.top = top;
        extent.height = rowHeight;
        auto toggle = std::shared_ptr<Control>(gui.newControl(ControlType::ToggleButton, addition.tag));
        toggle->setBorder(lastToggle->border());
        toggle->setHilight(lastToggle->hilight());
        toggle->setText(lastToggle->text());
        toggle->setTextMessage(addition.text);
        toggle->setPadding(lastToggle->padding());
        toggle->setTintBorderFill(gui.tintBorderFills());
        toggle->setAuthoredExtent(extent);
        toggle->setExtent(extent);
        auto added = std::static_pointer_cast<ToggleButton>(toggle);
        if (auto source = std::dynamic_pointer_cast<ToggleButton>(lastToggle)) {
            added->copyCheckedFill(*source);
        }
        gui.addControlToBack(toggle, IGUI::ControlCoordinates::Authored);
        top += pitch;
    }
    gui.refreshLayout();
}

// The Tier 2 check boxes go in a second column where the panel's (unused)
// description box is, styled as the first column's last check box and at its
// pitch. The box is hidden.
void addSideColumnToggles(IGUI &gui, const std::vector<AddedToggle> &additions) {
    auto box = gui.findControl("LB_DESC");
    auto upper = gui.findControl("CB_BLOOM");
    auto lower = gui.findControl("CB_AMBIENTOCC");
    if (additions.empty() || !box || !upper || !lower || gui.findControl(additions.front().tag)) {
        return;
    }
    auto style = std::dynamic_pointer_cast<ToggleButton>(lower);
    const auto boxExtent = box->authoredExtent();
    const auto rowExtent = lower->authoredExtent();
    int pitch = std::max(1, rowExtent.top - upper->authoredExtent().top);
    int top = boxExtent.top;
    if (auto first = gui.findControl("BTN_TEXQUAL")) {
        top = std::max(top, first->authoredExtent().top); // level with the first column
    }
    // The column may run past the box down to the panel's bottom buttons.
    int limit = std::numeric_limits<int>::max();
    for (const char *tag : {"BTN_DEFAULT", "BTN_BACK", "BTN_CANCEL", "BTN_OK", "LBL_BAR3"}) {
        if (auto control = gui.findControl(tag)) {
            const auto &e = control->authoredExtent();
            if (e.top > top) {
                limit = std::min(limit, e.top);
            }
        }
    }
    if (limit == std::numeric_limits<int>::max()) {
        limit = boxExtent.top + boxExtent.height;
    }
    int rowHeight = rowExtent.height;
    int count = static_cast<int>(additions.size());
    if (count > 0 && top + count * pitch > limit) {
        pitch = std::max(1, (limit - top) / count);
        rowHeight = std::min(rowHeight, pitch + pitch / 2);
    }
    for (const auto &addition : additions) {
        Control::Extent extent = rowExtent;
        extent.height = rowHeight;
        extent.left = boxExtent.left + (boxExtent.width - rowExtent.width) / 2;
        extent.top = top;
        auto toggle = std::shared_ptr<Control>(gui.newControl(ControlType::ToggleButton, addition.tag));
        toggle->setBorder(lower->border());
        toggle->setHilight(lower->hilight());
        toggle->setText(lower->text());
        toggle->setTextMessage(addition.text);
        toggle->setPadding(lower->padding());
        toggle->setTintBorderFill(gui.tintBorderFills());
        toggle->setAuthoredExtent(extent);
        toggle->setExtent(extent);
        if (style) {
            std::static_pointer_cast<ToggleButton>(toggle)->copyCheckedFill(*style);
        }
        gui.addControlToBack(toggle, IGUI::ControlCoordinates::Authored);
        top += pitch;
    }
    box->setVisible(false);
    gui.refreshLayout();
}

} // namespace

OptionsPanel::OptionsPanel(OptionsPanels &stack, Game &game, ServicesView &services, std::string base) :
    GameGUI(game, services),
    _stack(stack),
    _base(std::move(base)) {
    _resRef = _base;
}

std::string OptionsPanel::settingKey(const Control &control) const {
    std::string stem = panelStem(_base);
    if (isAdvancedGraphics(stem)) {
        stem = "optgraphicsadv"; // K2's panel drops the "s"; one setting for both
    }
    return stem + "." + control.tag();
}

int OptionsPanel::currentVolume(const std::string &key) const {
    auto &audio = _game.options().audio;
    if (key == "audio.music") return audio.musicVolume;
    if (key == "audio.voice") return audio.voiceVolume;
    if (key == "audio.sound") return audio.soundVolume;
    return audio.movieVolume;
}

void OptionsPanel::onGUILoaded() {
    if (!isPopup(_base)) {
        loadBackground(BackgroundType::Menu);
    }
    if (!isPopup(_base)) {
        centerRootInCanvas(isTSL() ? 800 : 640, isTSL() ? 600 : 480);
    }

    // The sub-panels each panel's buttons open.
    struct Link {
        const char *panel;
        const char *tag;
        std::string target;
    };
    const std::vector<Link> links {
        {"optionsmain", "BTN_GAMEPLAY", guiResRef("optgameplay")},
        {"optionsmain", "BTN_FEEDBACK", guiResRef("optfeedback")},
        {"optionsmain", "BTN_AUTOPAUSE", guiResRef("optautopause")},
        {"optionsmain", "BTN_GRAPHICS", guiResRef("optgraphics")},
        {"optionsmain", "BTN_SOUND", guiResRef("optsound")},
        {"optgameplay", "BTN_KEYMAP", guiResRef("optkeymapping")},
        {"optgameplay", "BTN_MOUSE", guiResRef("optmouse")},
        {"optgraphics", "BTN_RESOLUTION", guiResRef("optresolution")},
        // K2 names this panel without the "s".
        {"optgraphics", "BTN_ADVANCED", isTSL() ? guiResRef("optgraphicadv") : guiResRef("optgraphicsadv")},
        {"optsound", "BTN_ADVANCED", guiResRef("optsoundadv")},
    };
    for (const auto &link : links) {
        if (_base.rfind(link.panel, 0) != 0) {
            continue;
        }
        if (auto button = findControl<Button>(link.tag)) {
            std::string target = link.target;
            button->setOnClick([this, target]() {
                _stack.open(target);
            });
        }
    }

    for (const char *tag : kBackTags) {
        if (auto button = findControl<Button>(tag)) {
            button->setOnClick([this]() {
                _stack.back();
            });
        }
    }

    if (panelStem(_base) == "optgameplay") {
        // A host setting in co-op: a client sees it but cannot change it.
        auto *coop = _game.coop();
        addSmartCompanionsToggle(*_gui, coop && coop->isClient());
    }

    if (panelStem(_base) == "optgraphics") {
        addColumnToggles(*_gui, std::vector<AddedToggle>(std::begin(kGraphicsToggles), std::end(kGraphicsToggles)));
        if (auto toggle = std::dynamic_pointer_cast<ToggleButton>(_gui->findControl(kEnhancedTag))) {
            if (isTSL()) {
                toggle->setBorderColor(_baseColor);
                toggle->setHilightColor(_hilightColor);
                toggle->setOnColor(_baseColor);
            }
            toggle->setShowCheckedFill(true);
            _toggles.push_back(toggle);
            // The preset writes the feature settings it covers; the box shows
            // whether they all match it (it is not a setting of its own).
            toggle->setOnClick([this]() {
                bool enhanced = !graphics::matchesGraphicsPreset(_game.options().graphics, graphics::GraphicsPreset::Enhanced);
                graphics::GraphicsOptions preset;
                graphics::applyGraphicsPreset(preset, enhanced ? graphics::GraphicsPreset::Enhanced : graphics::GraphicsPreset::Retail);
                auto &settings = _game.userSettings();
                settings.setBool(kGraphicsBloomSetting, preset.bloom);
                settings.setBool(kGraphicsAOSetting, preset.gtao);
                settings.setBool(kGraphicsSoftShadowsSetting, preset.softShadows);
                settings.setBool(kGraphicsSharpenSetting, preset.sharpen);
                settings.setInt(kGraphicsAntiAliasingSetting, static_cast<int>(graphics::antiAliasing(preset)));
                for (const auto &feature : graphics::graphicsFeatureToggles()) {
                    settings.setBool(graphicsFeatureSetting(feature.settingTag), preset.*feature.field);
                }
                // High-res shadows and anisotropy are not part of a preset.
                _game.applyUserSettings(false);
                refresh();
            });
        }
    }
    if (isAdvancedGraphics(panelStem(_base))) {
        addColumnToggles(*_gui, std::vector<AddedToggle>(std::begin(kAdvancedToggles), std::end(kAdvancedToggles)));
        std::vector<AddedToggle> tier2;
        for (const auto &feature : graphics::graphicsFeatureToggles()) {
            tier2.push_back({feature.settingTag, feature.label});
        }
        addSideColumnToggles(*_gui, tier2);
        auto stepper = [this](const char *key, int count, int current, int delta) {
            int value = std::clamp(_game.userSettings().getInt(key, current) + delta, 0, count - 1);
            _game.userSettings().setInt(key, value);
            _game.applyUserSettings(false);
            refresh();
        };
        auto aaNow = [this]() { return static_cast<int>(graphics::antiAliasing(_game.options().graphics)); };
        auto anisoNow = [this]() { return _game.options().graphics.anisotropicFiltering; };
        if (auto left = findControl<Button>("BTN_ANTIALIASLEFT")) {
            left->setOnClick([stepper, aaNow]() { stepper(kGraphicsAntiAliasingSetting, 4, aaNow(), -1); });
        }
        if (auto right = findControl<Button>("BTN_ANTIALIASRIGHT")) {
            right->setOnClick([stepper, aaNow]() { stepper(kGraphicsAntiAliasingSetting, 4, aaNow(), 1); });
        }
        if (auto left = findControl<Button>("BTN_ANISOTROPYLEFT")) {
            left->setOnClick([stepper, anisoNow]() { stepper(kGraphicsAnisotropySetting, 5, anisoNow(), -1); });
        }
        if (auto right = findControl<Button>("BTN_ANISOTROPYRIGHT")) {
            right->setOnClick([stepper, anisoNow]() { stepper(kGraphicsAnisotropySetting, 5, anisoNow(), 1); });
        }
        for (const char *tag : {"BTN_ANTIALIAS", "BTN_ANISOTROPY"}) {
            if (auto button = findControl<Button>(tag)) {
                _stepperLabels[tag] = button->text().text;
                // K2 authors the label across its arrows: clicks go to the arrows.
                button->setSelectable(false);
            }
        }
    }

    // Check boxes are toggle buttons; each remembers its setting.
    std::vector<const char *> toggleTags {kSmartCompanionsTag, "CB_BLOOM", "CB_AMBIENTOCC", "CB_HIRESSHADOWS", "CB_SHARPEN", "CB_INVERTCAM", "CB_LEVELUP", "CB_AUTOSAVE", "CB_REVERSE", "CB_DISABLEMOVE",
                                          "CB_ENEMYSIGHTED", "CB_PARTYKILLED", "CB_ACTIONMENU", "CB_ENDROUND", "CB_TRIGGERS",
                                          "CB_MINESIGHTED", "CB_SHADOWS", "CB_GRASS", "CB_FRAMEBUFF", "CB_VSYNC",
                                          "CB_SOFTSHADOWS", "CB_FORCESOFTWARE", "CB_REVBUTTONS",
                                          "CB_REVERSE_INGAME", "CB_FORCESPEED"};
    if (isAdvancedGraphics(panelStem(_base))) {
        for (const auto &feature : graphics::graphicsFeatureToggles()) {
            toggleTags.push_back(feature.settingTag);
        }
    }
    for (const char *tag : toggleTags) {
        auto toggle = std::dynamic_pointer_cast<ToggleButton>(_gui->findControl(tag));
        if (!toggle) {
            continue;
        }
        if (isTSL()) {
            // K2 authors its check-box art as white masks with a white
            // border colour; tint them with the screen palette like its
            // other masks, or they draw as grey rings.
            toggle->setBorderColor(_baseColor);
            toggle->setHilightColor(_hilightColor);
            toggle->setOnColor(_baseColor); // the checked art too
        }
        toggle->setShowCheckedFill(true);
        _toggles.push_back(toggle);
        ToggleButton *raw = toggle.get();
        toggle->setOnClick([this, raw]() {
            raw->toggle();
            _game.userSettings().setBool(settingKey(*raw), raw->isOn());
            _game.applyUserSettings(false);
        });
    }
    // Volume sliders move and apply at once; the file is written on close.
    for (const auto &setting : kSliders) {
        auto slider = std::dynamic_pointer_cast<Slider>(_gui->findControl(setting.tag));
        if (!slider) {
            continue;
        }
        std::string key = setting.key;
        _sliders.push_back({slider, key});
        slider->setOnValueChanged([this, key](float value) {
            _game.userSettings().setInt(key, static_cast<int>(value * 100.0f + 0.5f));
            _game.applyUserSettings(false);
        });
    }

    // Difficulty: the arrows step through difficultyopt.2da's named rows.
    if (panelStem(_base) == "optgameplay") {
        auto stepDifficulty = [this](int delta) {
            int value = _game.userSettings().getInt("gameplay.difficulty", _game.options().game.clientDifficulty);
            value = std::clamp(value + delta, 0, 2);
            _game.userSettings().setInt("gameplay.difficulty", value);
            _game.applyUserSettings(false);
            refresh();
        };
        if (auto left = findControl<Button>("BTN_DIFFLEFT")) {
            left->setOnClick([stepDifficulty]() { stepDifficulty(-1); });
        }
        if (auto right = findControl<Button>("BTN_DIFFRIGHT")) {
            right->setOnClick([stepDifficulty]() { stepDifficulty(1); });
        }
    }

    // Key mapping: the bindable actions and their keys.
    if (panelStem(_base) == "optkeymapping") {
        if (auto events = findControl<ListBox>("LST_EventList")) {
            events->setOnItemDoubleClick([this](const std::string &tag) {
                auto action = keyActionByName(tag);
                if (action && keyActionInfo(*action).remappable) {
                    _stack.beginKeyEntry(*action);
                }
            });
        }
    }

    for (const char *tag : kDefaultTags) {
        if (auto button = findControl<Button>(tag)) {
            button->setOnClick([this]() {
                auto &settings = _game.userSettings();
                for (auto &toggle : _toggles) {
                    settings.erase(settingKey(*toggle));
                }
                for (auto &slider : _sliders) {
                    settings.erase(slider.key);
                }
                if (panelStem(_base) == "optgameplay") {
                    settings.erase("gameplay.difficulty");
                }
                if (isAdvancedGraphics(panelStem(_base))) {
                    settings.erase(kGraphicsAntiAliasingSetting);
                    settings.erase(kGraphicsAnisotropySetting);
                }
                if (panelStem(_base) == "optkeymapping") {
                    _game.keymap().resetToDefaults();
                    _game.keymap().storeSettings(settings);
                    _game.padBindings().resetToDefaults();
                    _game.padBindings().storeSettings(settings);
                }
                _game.applyUserSettings(false);
                refresh();
            });
        }
    }
    refresh();
}

void OptionsPanel::refreshKeyList() {
    auto events = findControl<ListBox>("LST_EventList");
    if (!events) {
        return;
    }
    std::shared_ptr<resource::TwoDA> table;
    try {
        table = _services.resource.twoDas.get("keymap");
    } catch (const std::exception &) {
    }
    events->clearItems();
    for (const auto &info : keyActionInfos()) {
        std::string label = info.name;
        if (table) {
            for (int row = 0; row < table->getRowCount(); ++row) {
                if (table->getString(row, "name") == info.name) {
                    int strref = table->getInt(row, "actionstrref", -1);
                    if (strref >= 0) {
                        label = _services.resource.strings.getText(strref);
                    }
                    break;
                }
            }
        }
        ListBox::Item item;
        item.tag = info.name;
        std::string text = label + ": " + keyName(_game.keymap().keyFor(info.action));
        if (PadBindings::bindable(info.action)) {
            auto button = _game.padBindings().buttonFor(info.action);
            text += "  [" + (button ? gamepadButtonName(*button) : std::string("-")) + "]";
        }
        item.text = std::move(text);
        events->addItem(std::move(item));
    }
}

void OptionsPanel::refresh() {
    auto &settings = _game.userSettings();
    for (auto &toggle : _toggles) {
        bool on = settings.getBool(settingKey(*toggle), toggleDefault(toggle->tag()));
        if (toggle->isOn() != on) {
            toggle->toggle();
        }
    }
    for (auto &slider : _sliders) {
        slider.control->setValue(currentVolume(slider.key) / 100.0f);
    }
    if (panelStem(_base) == "optgameplay") {
        if (auto label = findControl<Button>("BTN_DIFFICULTY")) {
            int value = _game.options().game.clientDifficulty;
            std::string name;
            try {
                if (auto table = _services.resource.twoDas.get("difficultyopt")) {
                    int strref = table->getInt(value, "name", -1);
                    if (strref >= 0) {
                        name = _services.resource.strings.getText(strref);
                    }
                }
            } catch (const std::exception &) {
            }
            label->setTextMessage(name.empty() ? std::to_string(value) : name);
        }
    }
    if (panelStem(_base) == "optkeymapping") {
        refreshKeyList();
    }
    if (isAdvancedGraphics(panelStem(_base))) {
        const auto &gfx = _game.options().graphics;
        auto label = [this](const char *tag, const std::string &value) {
            auto button = findControl<Button>(tag);
            auto authored = _stepperLabels.find(tag);
            if (button && authored != _stepperLabels.end()) {
                button->setTextMessage(authored->second + ": " + value);
            }
        };
        label("BTN_ANTIALIAS", kAntiAliasingNames[static_cast<int>(graphics::antiAliasing(gfx))]);
        label("BTN_ANISOTROPY", kAnisotropyNames[std::clamp(gfx.anisotropicFiltering, 0, 4)]);
    }
}

bool OptionsPanel::toggleDefault(const std::string &tag) const {
    // Check boxes that mirror a running option start as that option.
    auto &graphics = _game.options().graphics; // already the setting or its launch value
    if (tag == "CB_GRASS") return graphics.grass;
    if (tag == "CB_SHADOWS") return graphics.shadows;
    if (tag == "CB_VSYNC") return graphics.vsync;
    if (tag == kEnhancedTag) return graphics::matchesGraphicsPreset(graphics, graphics::GraphicsPreset::Enhanced);
    if (tag == "CB_BLOOM") return graphics.bloom;
    if (tag == "CB_AMBIENTOCC") return graphics.gtao;
    if (tag == "CB_SOFTSHADOWS") return graphics.softShadows;
    if (tag == "CB_HIRESSHADOWS") return graphics.shadowResolution > 2048;
    if (tag == "CB_SHARPEN") return graphics.sharpen;
    if (tag == kSmartCompanionsTag) return _game.options().game.smartCompanions;
    for (const auto &feature : graphics::graphicsFeatureToggles()) {
        if (tag == feature.settingTag) return graphics.*feature.field;
    }
    return false;
}

bool OptionsPanel::handle(const input::Event &event) {
    // The key-entry popup takes the next key as the new binding.
    if (_stack.keyEntryAction() && event.type == input::EventType::KeyDown && !event.key.repeat &&
        panelStem(_base) == "optkeyentry") {
        if (event.key.code != input::KeyCode::Escape) {
            _game.keymap().bind(*_stack.keyEntryAction(), event.key.code);
            _game.keymap().storeSettings(_game.userSettings());
            _game.applyUserSettings(false);
        }
        _stack.back();
        return true;
    }
    if (event.type == input::EventType::KeyDown && !event.key.repeat &&
        event.key.code == input::KeyCode::Escape) {
        _stack.back();
        return true;
    }
    return GameGUI::handle(event);
}

std::string OptionsPanels::panelResRef(const std::string &requested) const {
    // Panels follow the menu profile, whichever main-menu theme asked: a K1
    // file served through the K2 loader resolves its K1 strrefs against
    // K2's TLK ("Gender Required" for Sound).
    if (_game.hudPresentationId() == resource::GameID::TSL && panelStem(requested) == requested) {
        return requested + "_p";
    }
    return requested;
}

bool OptionsPanels::open(const std::string &requested) {
    std::string base = panelResRef(requested);
    auto it = _panels.find(base);
    if (it == _panels.end()) {
        auto panel = std::make_unique<OptionsPanel>(*this, _game, _services, base);
        try {
            panel->init();
        } catch (const std::exception &e) {
            warn("Options: panel " + base + " did not load: " + e.what());
            return false;
        }
        it = _panels.emplace(base, std::move(panel)).first;
    }
    it->second->refresh();
    _stack.push_back(base);
    return true;
}

void OptionsPanels::beginKeyEntry(KeyAction action) {
    // A pad button pressed now binds the pad side of this action; Start cancels, as Escape does.
    _game.setPadCapture([this](input::GamepadButton button) {
        if (!_keyEntry) {
            return false;
        }
        if (button != input::GamepadButton::Start && button != input::GamepadButton::Guide) {
            _game.padBindings().bind(*_keyEntry, button);
            _game.padBindings().storeSettings(_game.userSettings());
            _game.applyUserSettings(false);
        }
        back();
        return true;
    });
    auto current = active();
    std::string keyEntry = current && current->base().size() > 2 && current->base()[current->base().size() - 2] == '_'
                               ? "optkeyentry" + current->base().substr(current->base().size() - 2)
                               : "optkeyentry";
    _keyEntry = action;
    if (!open(keyEntry)) {
        _keyEntry.reset();
        _game.setPadCapture(nullptr);
    }
}

void OptionsPanels::back() {
    if (_stack.empty()) {
        return;
    }
    bool leftKeyEntry = panelStem(_stack.back()) == "optkeyentry";
    _stack.pop_back();
    if (leftKeyEntry) {
        _keyEntry.reset();
        _game.setPadCapture(nullptr);
    }
    // Leaving a panel keeps what it set.
    _game.userSettings().save();
    if (auto panel = active()) {
        panel->refresh();
    }
    if (_stack.empty() && _onClosed) {
        _onClosed();
    }
}

void OptionsPanels::closeAll() {
    bool wasOpen = !_stack.empty();
    _stack.clear();
    _keyEntry.reset();
    _game.setPadCapture(nullptr);
    if (wasOpen && _onClosed) {
        _onClosed();
    }
}

OptionsPanel *OptionsPanels::active() const {
    if (_stack.empty()) {
        return nullptr;
    }
    auto it = _panels.find(_stack.back());
    return it != _panels.end() ? it->second.get() : nullptr;
}

bool OptionsPanels::handle(const input::Event &event) {
    auto panel = active();
    return panel && panel->handle(event);
}

void OptionsPanels::update(float dt) {
    if (auto panel = active()) {
        panel->update(dt);
    }
}

void OptionsPanels::render() {
    if (_stack.empty()) {
        return;
    }
    // A popup panel draws over the panel that opened it.
    size_t first = _stack.size() - 1;
    while (first > 0 && isPopup(_stack[first])) {
        --first;
    }
    for (size_t i = first; i < _stack.size(); ++i) {
        auto it = _panels.find(_stack[i]);
        if (it != _panels.end()) {
            it->second->render();
        }
    }
}

} // namespace game

} // namespace reone
