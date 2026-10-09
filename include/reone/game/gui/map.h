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

#include "reone/graphics/texture.h"

#include "reone/resource/parser/gff/are.h"

namespace reone {

namespace resource {

class Gff;

}

namespace game {

struct ServicesView;

class Game;
class Waypoint;

class Map {
public:
    enum class Mode {
        Default,
        Minimap
    };

    Map(Game &game, ServicesView &services);

    void load(const std::string &area, const resource::generated::ARE_Map &map);

    void retireRuntimeSession();

    /**
     * @param scale the factor the owning GUI draws its layout at.
     *
     * The map and its markers are drawn into a control's rect but not by the
     * control, so nothing else carries the layout scale to them: the frame
     * shrank with the GUI while the map inside it stayed native size.
     */
    void render(Mode mode, const glm::vec4 &bounds, float scale);

    bool isLoaded() const { return static_cast<bool>(_areaTexture); }

    void setSelectedNote(std::shared_ptr<Waypoint> waypoint) { _selectedNote = std::move(waypoint); }

private:
    Game &_game;
    ServicesView &_services;

    int _northAxis {0};
    glm::vec2 _worldPoint1 {0.0f};
    glm::vec2 _worldPoint2 {0.0f};
    glm::vec2 _mapPoint1 {0.0f};
    glm::vec2 _mapPoint2 {0.0f};

    std::shared_ptr<graphics::Texture> _areaTexture;
    std::shared_ptr<graphics::Texture> _arrowTexture;
    std::shared_ptr<graphics::Texture> _noteTexture;
    std::shared_ptr<graphics::Texture> _memberTexture;

    std::shared_ptr<Waypoint> _selectedNote;
    // Fog over unexplored cells (design note p1_game "Map fog of war").
    std::shared_ptr<graphics::Texture> _fogTexture;
    int _fogColumns {0};
    int _fogRows {0};
    int _fogExplored {-1};

    void loadProperties(const resource::generated::ARE_Map &map);
    void loadTextures(const std::string &area);

    void renderArea(Mode mode, const glm::vec4 &bounds, float scale);
    void renderPartyLeader(Mode mode, const glm::vec4 &bounds, float scale);
    /** Party members and co-op players' characters (design note p2_ui, "Party markers"). */
    void renderPartyMembers(Mode mode, const glm::vec4 &bounds, float scale);
    /** Where a world point sits on screen in `mode`, relative to the leader on the minimap. */
    glm::vec2 markerScreenPosition(Mode mode, const glm::vec4 &bounds, float scale, const glm::vec2 &world, const glm::vec2 &leaderWorld) const;
    void renderNotes(Mode mode, const glm::vec4 &bounds, float scale);
    /** Draw the fog over the map's logical rect (x, y, width, height). */
    void renderFog(const glm::vec4 &logicalRect);
    bool updateFogTexture();

    glm::vec2 getMapPosition(const glm::vec2 &world) const;

    /** Converts a normalized map position into logical map-canvas pixels. */
    void normalizeMapPosition(glm::vec2 &mapPos) const;
};

} // namespace game

} // namespace reone
