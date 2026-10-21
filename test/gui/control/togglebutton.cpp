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

#include <gtest/gtest.h>

#include "../../fixtures/graphics.h"
#include "../../fixtures/gui.h"
#include "../../fixtures/resource.h"
#include "../../fixtures/scene.h"
#include "reone/graphics/texture.h"
#include "reone/gui/control/togglebutton.h"

using namespace reone;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;
using namespace reone::scene;
using namespace testing;

namespace {

class ToggleButtonTest : public Test {
protected:
    void SetUp() override {
        _graphicsModule.init();
        _resourceModule.init();
        _sceneModule.init();
        _off = std::make_shared<Texture>("off", TextureType::TwoDim, Texture::Properties {});
        _on = std::make_shared<Texture>("on", TextureType::TwoDim, Texture::Properties {});
        ON_CALL(_resourceModule.textures(), get("off", TextureUsage::GUI)).WillByDefault(Return(_off));
        ON_CALL(_resourceModule.textures(), get("on", TextureUsage::GUI)).WillByDefault(Return(_on));
    }

    std::unique_ptr<ToggleButton> newToggle(bool withCheckedArt) {
        resource::generated::GUI_CONTROLS control;
        control.CONTROLTYPE = static_cast<int>(ControlType::ToggleButton);
        control.EXTENT.WIDTH = 100;
        control.EXTENT.HEIGHT = 40;
        control.BORDER.FILL = "off";
        if (withCheckedArt) {
            control.SELECTED.emplace();
            control.SELECTED->FILL = "on";
        }
        auto toggle = std::make_unique<ToggleButton>(
            _gui, _sceneModule.graphs(), _graphicsModule.services(), _resourceModule.services());
        toggle->load(control, false);
        return toggle;
    }

    NiceMock<MockGUI> _gui;
    TestGraphicsModule _graphicsModule;
    TestResourceModule _resourceModule;
    TestSceneModule _sceneModule;
    std::shared_ptr<Texture> _off;
    std::shared_ptr<Texture> _on;
};

TEST_F(ToggleButtonTest, shows_checked_art_only_when_asked) {
    auto toggle = newToggle(true);
    EXPECT_TRUE(toggle->hasCheckedFill());

    toggle->toggle();
    EXPECT_EQ(toggle->border().fill, _off);

    toggle->setShowCheckedFill(true);
    EXPECT_EQ(toggle->border().fill, _on); // already on: shown at once
    toggle->toggle();
    EXPECT_EQ(toggle->border().fill, _off);
}

// A check box added in code carries no checked art of its own; without a
// copy it never drew its on state (the "Smarter companions" box).
TEST_F(ToggleButtonTest, copied_checked_art_draws_the_on_state_of_an_added_box) {
    auto authored = newToggle(true);
    auto added = newToggle(false);
    EXPECT_FALSE(added->hasCheckedFill());
    added->setShowCheckedFill(true);
    added->toggle();
    EXPECT_EQ(added->border().fill, _off);

    added->copyCheckedFill(*authored);
    authored->setShowCheckedFill(true);
    added->setShowCheckedFill(true);
    EXPECT_TRUE(added->isOn());
    EXPECT_EQ(added->border().fill, _on);
    added->toggle();
    EXPECT_FALSE(added->isOn());
    EXPECT_EQ(added->border().fill, _off);
}

TEST_F(ToggleButtonTest, on_text_colour_applies_while_on_and_restores_when_off) {
    auto toggle = newToggle(true);
    toggle->setTextColor(glm::vec3(0.1f, 0.2f, 0.3f));
    toggle->setOnTextColor(glm::vec3(1.0f));
    EXPECT_EQ(toggle->text().color, glm::vec3(0.1f, 0.2f, 0.3f));
    toggle->toggle();
    EXPECT_EQ(toggle->text().color, glm::vec3(1.0f));
    toggle->toggle();
    EXPECT_EQ(toggle->text().color, glm::vec3(0.1f, 0.2f, 0.3f));
}

} // namespace
