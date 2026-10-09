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

#include "reone/graphics/font.h"
#include "reone/graphics/texture.h"
#include "reone/input/event.h"

#include "../contextaction.h"
#include "actionslot.h"

namespace reone {

namespace resource {
class IStrings;
}

namespace game {

struct ServicesView;

class Game;
class Object;

/**
 * The "empty" mark on a looted container's title (a K2 feature, shown in
 * both games): a container placeable or a dead creature with nothing left
 * to take.
 */
namespace containerlabel {

/** K2 dialog.tlk: "Empty". K1 has no string of its own for it. */
constexpr int kK2EmptyStrRef = 125550;

bool isEmptyContainer(const Object &object);

/** K2's word (its own game, or the K2 install next to K1), else ours. */
std::string emptyWord(resource::IStrings &strings, bool tsl);

/** "Footlocker (Empty)" for an empty container, the plain name otherwise. */
std::string title(const Object &object, const std::string &emptyWord);

} // namespace containerlabel

class SelectionOverlay {
    friend class TestGameModule;

public:
    SelectionOverlay(
        Game &game,
        ServicesView &services);

    void init();

    bool handle(const input::Event &event);
    /** Act with a target action slot's current choice, as clicking it does. */
    bool activateActionSlot(int index);
    void update();
    void render();

private:
    enum class ActionBand {
        None,
        Previous,
        Icon,
        Next
    };

    Game &_game;
    ServicesView &_services;

    std::shared_ptr<graphics::Font> _font;
    std::shared_ptr<graphics::Texture> _friendlyReticle;
    std::shared_ptr<graphics::Texture> _friendlyReticle2;
    std::shared_ptr<graphics::Texture> _hostileReticle;
    std::shared_ptr<graphics::Texture> _hostileReticle2;
    std::shared_ptr<graphics::Texture> _friendlyScroll;
    std::shared_ptr<graphics::Texture> _hostileScroll;
    std::shared_ptr<graphics::Texture> _hilightedScroll;
    std::shared_ptr<graphics::Texture> _actionArrow;
    std::shared_ptr<graphics::Texture> _hilightedActionArrow;
    std::shared_ptr<Object> _hilightedObject;
    std::shared_ptr<Object> _selectedObject;
    std::vector<ActionSlot> _actionSlots;
    glm::vec3 _hilightedScreenCoords {0.0f};
    glm::vec3 _selectedScreenCoords {0.0f};
    int _reticleHeight {0};
    int _selectedActionSlot {-1};
    ActionBand _hilightedActionBand {ActionBand::None};
    bool _hilightedHostile {false};
    bool _selectedHostile {false};
    bool _hasActions {false};
    /// Whether the HUD presents as K2 (colours and title text scale).
    bool _presentationTSL {false};

    bool handleMouseMotion(const input::MouseMotionEvent &event);
    bool handleMouseButtonDown(const input::MouseButtonEvent &event);
    bool handleMouseWheel(const input::MouseWheelEvent &event);

    void renderReticle(std::shared_ptr<graphics::Texture> texture, const glm::vec3 &screenCoords);
    void renderTitleBar();
    void renderHealthBar();
    void renderActionBar();

    void renderActionFrame(int index);
    void renderActionArrows(int index);
    void renderActionArrow(int index, bool previous);
    void renderActionIcon(int index);

    bool getActionScreenCoords(int index, float &x, float &y) const;
    /** Screen point the target action frames hang from: the reticle, or the fixed dock. */
    glm::vec2 actionAnchor() const;
    float layoutScale() const;
    glm::vec3 getColorFromSelectedObject() const;
};

} // namespace game

} // namespace reone
