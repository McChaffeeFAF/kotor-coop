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

#include "reone/game/gui/map.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/module.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/uniforms.h"
#include "reone/game/coopsession.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/textures.h"
#include "reone/system/logutil.h"

using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kArrowSize = 32;
static constexpr int kMapNoteSize = 16;
static constexpr float kSelectedMapNoteScale = 1.5f;
static constexpr float kKotorMapWidth = 440.0f;
static constexpr float kTSLMapWidth = 512.0f;
static constexpr float kMapHeight = 256.0f;

Map::Map(Game &game, ServicesView &services) :
    _game(game),
    _services(services) {
}

void Map::load(const std::string &area, const resource::generated::ARE_Map &map) {
    loadProperties(map);
    loadTextures(area);
}

void Map::retireRuntimeSession() {
    _areaTexture.reset();
    _selectedNote.reset();
}

void Map::loadProperties(const resource::generated::ARE_Map &map) {
    _northAxis = map.NorthAxis;
    _worldPoint1 = glm::vec2(map.WorldPt1X, map.WorldPt1Y);
    _worldPoint2 = glm::vec2(map.WorldPt2X, map.WorldPt2Y);
    _mapPoint1 = glm::vec2(map.MapPt1X, map.MapPt1Y);
    _mapPoint2 = glm::vec2(map.MapPt2X, map.MapPt2Y);
}

void Map::loadTextures(const std::string &area) {
    std::string resRef("lbl_map" + area);
    _areaTexture = _services.resource.textures.get(resRef, TextureUsage::GUI);

    if (!_arrowTexture) {
        // The party-leader arrow is HUD art and follows the HUD profile; the
        // area map itself is the running game's content.
        auto presentation = _game.hudPresentation();
        _arrowTexture = presentation.resource.textures.get(
            presentation.isTSL() ? "mm_barrow_p" : "mm_barrow", TextureUsage::GUI);
    }
    if (!_noteTexture) {
        _noteTexture = _services.resource.textures.get("whitetarget", TextureUsage::GUI);
    }
    if (!_memberTexture) {
        // Both games ship this 16 px ring for party members.
        auto presentation = _game.hudPresentation();
        _memberTexture = presentation.resource.textures.get("lbl_mapcircle", TextureUsage::GUI);
    }
}

void Map::render(Mode mode, const glm::vec4 &bounds, float scale) {
    if (!_areaTexture) {
        return;
    }
    _services.graphics.context.withBlendMode(BlendMode::Normal, [this, &mode, &bounds, scale]() {
        renderArea(mode, bounds, scale);
        renderNotes(mode, bounds, scale);
        renderPartyMembers(mode, bounds, scale);
        renderPartyLeader(mode, bounds, scale);
    });
}

void Map::renderArea(Mode mode, const glm::vec4 &bounds, float scale) {
    if (mode == Mode::Minimap) {
        std::shared_ptr<Creature> partyLeader(_game.party().getLeader());
        if (!partyLeader) {
            return;
        }
        _services.graphics.context.bindTexture(*_areaTexture);

        glm::vec2 worldPos(partyLeader->position());
        glm::vec2 mapPos(getMapPosition(worldPos));

        // Drawn at the layout's scale, not the texture's native size: the
        // minimap frame is a control and shrinks with the GUI, so a native-size
        // map inside it is the wrong zoom and slides under its own scissor.
        // The offsets that centre the party leader are in the same pixel space
        // and take the same factor.
        const float mapWidth = _areaTexture->width() * scale;
        const float mapHeight = _areaTexture->height() * scale;

        glm::vec3 topLeft(0.0f);
        float logicalMapWidth = _game.isTSL() ? kTSLMapWidth : kKotorMapWidth;
        topLeft.x = bounds[0] + 0.5f * bounds[2] - mapPos.x * logicalMapWidth * scale;
        topLeft.y = bounds[1] + 0.5f * bounds[3] - mapPos.y * mapHeight;

        glm::mat4 transform(1.0f);
        transform = glm::translate(transform, topLeft);
        transform = glm::scale(transform, glm::vec3(mapWidth, mapHeight, 1.0f));

        _services.graphics.uniforms.setLocals([transform](auto &locals) {
            locals.reset();
            locals.model = std::move(transform);
        });
        _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpTexture));

        int height = _game.options().graphics.height;
        glm::ivec4 scissorBounds(bounds[0], height - (bounds[1] + bounds[3]), bounds[2], bounds[3]);
        _services.graphics.context.withScissorTest(scissorBounds, [&]() {
            _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
            renderFog(glm::vec4(topLeft.x, topLeft.y, logicalMapWidth * scale, kMapHeight * scale));
        });

    } else {
        _services.graphics.context.bindTexture(*_areaTexture);

        glm::mat4 transform(1.0f);
        transform = glm::translate(transform, glm::vec3(bounds[0], bounds[1], 0.0f));
        transform = glm::scale(transform, glm::vec3(bounds[2], bounds[3], 1.0f));

        _services.graphics.uniforms.setLocals([transform](auto &locals) {
            locals.reset();
            locals.model = std::move(transform);
        });
        _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpTexture));
        _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);

        float logicalMapWidth = _game.isTSL() ? kTSLMapWidth : kKotorMapWidth;
        renderFog(glm::vec4(bounds[0], bounds[1],
                            bounds[2] * logicalMapWidth / static_cast<float>(_areaTexture->width()),
                            bounds[3] * kMapHeight / static_cast<float>(_areaTexture->height())));
    }
}

bool Map::updateFogTexture() {
    auto module = _game.module();
    if (!module || !module->area()) {
        return false;
    }
    const auto &exploration = module->area()->mapExploration();
    int columns = exploration.columns();
    int rows = exploration.rows();
    int explored = exploration.exploredCount();
    if (columns <= 0 || rows <= 0) {
        return false;
    }
    if (_fogTexture && columns == _fogColumns && rows == _fogRows && explored == _fogExplored) {
        return true;
    }
    auto pixels = std::make_shared<ByteBuffer>(static_cast<size_t>(columns) * rows * 4, 0);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            if (!exploration.isExplored(column, row)) {
                // Texture rows run bottom up on the map quad, like the area
                // texture's: grid row 0 (the top) is the last texel row.
                size_t texelRow = static_cast<size_t>(rows - 1 - row);
                (*pixels)[(texelRow * columns + column) * 4 + 3] = static_cast<char>(0xff);
            }
        }
    }
    bool fresh = !_fogTexture || columns != _fogColumns || rows != _fogRows;
    if (fresh) {
        Texture::Properties properties;
        properties.minFilter = Texture::Filtering::Linear;
        properties.magFilter = Texture::Filtering::Linear;
        properties.wrap = Texture::Wrapping::ClampToEdge;
        _fogTexture = std::make_shared<Texture>("map_fog", TextureType::TwoDim, properties);
        _fogTexture->setPixels(columns, rows, PixelFormat::RGBA8, Texture::Layer {std::move(pixels)});
        _fogTexture->init();
    } else {
        // The upload goes to the bound texture: bind the fog first, or the
        // new cells land in whatever was bound last (the area map itself).
        _services.graphics.context.bindTexture(*_fogTexture);
        _fogTexture->setPixels(columns, rows, PixelFormat::RGBA8, Texture::Layer {std::move(pixels)}, true);
    }
    _fogColumns = columns;
    _fogRows = rows;
    _fogExplored = explored;
    return true;
}

void Map::renderFog(const glm::vec4 &logicalRect) {
    if (!updateFogTexture() || _fogExplored == _fogColumns * _fogRows) {
        return;
    }
    // Each cell covers an equal share of the map's logical rect. The fog is
    // filtered, so the edge of the explored region is soft.
    _services.graphics.context.bindTexture(*_fogTexture);
    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, glm::vec3(logicalRect[0], logicalRect[1], 0.0f));
    transform = glm::scale(transform, glm::vec3(logicalRect[2], logicalRect[3], 1.0f));
    _services.graphics.uniforms.setLocals([transform](auto &locals) {
        locals.reset();
        locals.model = transform;
    });
    _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpTexture));
    _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
}

void Map::renderNotes(Mode mode, const glm::vec4 &bounds, float scale) {
    if (mode != Mode::Default) {
        return;
    }
    _services.graphics.context.bindTexture(*_noteTexture);

    for (auto &object : _game.module()->area()->getObjectsByType(ObjectType::Waypoint)) {
        auto waypoint = std::static_pointer_cast<Waypoint>(object);
        if (!waypoint->isMapNoteEnabled() || waypoint->mapNote().empty())
            continue;

        glm::vec2 mapPos(getMapPosition(waypoint->position()));
        normalizeMapPosition(mapPos);

        glm::vec2 notePos;
        notePos.x = bounds[0] + mapPos.x * bounds[2];
        notePos.y = bounds[1] + mapPos.y * bounds[3];

        bool selected = waypoint == _selectedNote;
        float noteSize = (selected ? kSelectedMapNoteScale : 1.0f) * kMapNoteSize * scale;

        glm::mat4 transform(1.0f);
        transform = glm::translate(transform, glm::vec3(notePos.x - 0.5f * noteSize, notePos.y - 0.5f * noteSize, 0.0f));
        transform = glm::scale(transform, glm::vec3(noteSize, noteSize, 1.0f));

        auto guiColorHilight = _game.isTSL() ? kTSLGUIColorHilight : kGUIColorHilight;
        auto guiColorBase = _game.isTSL() ? kTSLGUIColorBase : kGUIColorBase;

        _services.graphics.uniforms.setLocals([&](auto &locals) {
            locals.reset();
            locals.model = std::move(transform);
            locals.color = glm::vec4(selected ? guiColorHilight : guiColorBase, 1.0f);
        });
        _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpIcon));
        _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
    }
}

void Map::normalizeMapPosition(glm::vec2 &mapPos) const {
    mapPos.x *= (_game.isTSL() ? kTSLMapWidth : kKotorMapWidth) /
                static_cast<float>(_areaTexture->width());
    mapPos.y *= kMapHeight / static_cast<float>(_areaTexture->height());
}

glm::vec2 Map::getMapPosition(const glm::vec2 &world) const {
    float scaleX, scaleY;
    glm::vec2 result(0.0f);

    switch (_northAxis) {
    case 0:
    case 1:
        scaleX = (_mapPoint1.x - _mapPoint2.x) / (_worldPoint1.x - _worldPoint2.x);
        scaleY = (_mapPoint1.y - _mapPoint2.y) / (_worldPoint1.y - _worldPoint2.y);
        result.x = (world.x - _worldPoint1.x) * scaleX + _mapPoint1.x;
        result.y = (world.y - _worldPoint1.y) * scaleY + _mapPoint1.y;
        break;
    case 2:
    case 3:
        scaleX = (_mapPoint1.y - _mapPoint2.y) / (_worldPoint1.x - _worldPoint2.x);
        scaleY = (_mapPoint1.x - _mapPoint2.x) / (_worldPoint1.y - _worldPoint2.y);
        result.x = (world.y - _worldPoint1.y) * scaleY + _mapPoint1.x;
        result.y = (world.x - _worldPoint1.x) * scaleX + _mapPoint1.y;
        break;
    default:
        warn("Map: invalid north axis: " + std::to_string(_northAxis));
        break;
    }

    return result;
}

glm::vec2 Map::markerScreenPosition(Mode mode, const glm::vec4 &bounds, float scale, const glm::vec2 &world, const glm::vec2 &leaderWorld) const {
    glm::vec2 mapPos(getMapPosition(world));
    if (mode == Mode::Default) {
        normalizeMapPosition(mapPos);
        return glm::vec2(bounds[0] + mapPos.x * bounds[2], bounds[1] + mapPos.y * bounds[3]);
    }
    // The minimap centres the leader and draws the map at the layout scale
    // (see renderArea); a marker sits at its offset from the leader.
    glm::vec2 leaderPos(getMapPosition(leaderWorld));
    float logicalMapWidth = _game.isTSL() ? kTSLMapWidth : kKotorMapWidth;
    return glm::vec2(
        bounds[0] + 0.5f * bounds[2] + (mapPos.x - leaderPos.x) * logicalMapWidth * scale,
        bounds[1] + 0.5f * bounds[3] + (mapPos.y - leaderPos.y) * _areaTexture->height() * scale);
}

// K2 rings party members in orange; co-op players' characters take a blue
// ring of our own (design note p2_ui, "Party markers").
static const glm::vec3 kPartyMemberMarkerColor(0.98f, 0.45f, 0.0f);
static const glm::vec3 kCoopPlayerMarkerColor(0.25f, 0.75f, 1.0f);
static constexpr int kMemberMarkerSize = 16;

void Map::renderPartyMembers(Mode mode, const glm::vec4 &bounds, float scale) {
    auto leader = _game.party().getLeader();
    if (!leader || !_memberTexture) {
        return;
    }
    // Party members first, then the co-op markers, which win for a character
    // that is both.
    std::vector<std::pair<std::shared_ptr<Object>, bool>> markers;
    auto addMarker = [&](const std::shared_ptr<Object> &object, bool player) {
        if (!object || object == leader || object->isDead()) {
            return;
        }
        for (auto &marker : markers) {
            if (marker.first == object) {
                marker.second = marker.second || player;
                return;
            }
        }
        markers.emplace_back(object, player);
    };
    auto &party = _game.party();
    for (int i = 0; i < party.getSize(); ++i) {
        addMarker(party.getMember(i), false);
    }
    if (auto coop = _game.coop()) {
        for (const auto &marker : coop->mapMarkers()) {
            addMarker(marker.object, marker.player);
        }
    }
    if (markers.empty()) {
        return;
    }
    const float size = kMemberMarkerSize * scale;
    auto draw = [&]() {
        _services.graphics.context.bindTexture(*_memberTexture);
        for (const auto &[object, player] : markers) {
            glm::vec2 pos = markerScreenPosition(mode, bounds, scale, glm::vec2(object->position()), glm::vec2(leader->position()));
            // Off the minimap: no marker. (A scissor pass would clear the
            // map already drawn there.)
            if (mode == Mode::Minimap &&
                (pos.x - 0.5f * size < bounds[0] || pos.x + 0.5f * size > bounds[0] + bounds[2] ||
                 pos.y - 0.5f * size < bounds[1] || pos.y + 0.5f * size > bounds[1] + bounds[3])) {
                continue;
            }
            glm::mat4 transform(1.0f);
            transform = glm::translate(transform, glm::vec3(pos.x - 0.5f * size, pos.y - 0.5f * size, 0.0f));
            transform = glm::scale(transform, glm::vec3(size, size, 1.0f));
            glm::vec3 color = player ? kCoopPlayerMarkerColor : kPartyMemberMarkerColor;
            _services.graphics.uniforms.setLocals([&](auto &locals) {
                locals.reset();
                locals.model = transform;
                locals.color = glm::vec4(color, 1.0f);
            });
            _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpIcon));
            _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
        }
    };
    draw();
}

void Map::renderPartyLeader(Mode mode, const glm::vec4 &bounds, float scale) {
    std::shared_ptr<Creature> partyLeader(_game.party().getLeader());
    if (!partyLeader) {
        return;
    }
    _services.graphics.context.bindTexture(*_arrowTexture);

    glm::vec3 arrowPos(0.0f);

    if (mode == Mode::Default) {
        glm::vec2 worldPos(partyLeader->position());
        glm::vec2 mapPos(getMapPosition(worldPos));
        normalizeMapPosition(mapPos);

        arrowPos.x = bounds[0] + mapPos.x * bounds[2];
        arrowPos.y = bounds[1] + mapPos.y * bounds[3];

    } else {
        arrowPos.x = bounds[0] + 0.5f * bounds[2];
        arrowPos.y = bounds[1] + 0.5f * bounds[3];
    }

    float facing;
    switch (_northAxis) {
    case 0:
        facing = -partyLeader->getFacing();
        break;
    case 1:
        facing = glm::pi<float>() - partyLeader->getFacing();
        break;
    case 2:
        facing = glm::three_over_two_pi<float>() - partyLeader->getFacing();
        break;
    default:
        facing = glm::half_pi<float>() - partyLeader->getFacing();
        break;
    }
    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, arrowPos);
    transform = glm::rotate(transform, facing, glm::vec3(0.0f, 0.0f, 1.0f));
    const float arrowSize = kArrowSize * scale;
    transform = glm::translate(transform, glm::vec3(-0.5f * arrowSize, -0.5f * arrowSize, 0.0f));
    transform = glm::scale(transform, glm::vec3(arrowSize, arrowSize, 1.0f));

    _services.graphics.uniforms.setLocals([this, transform](auto &locals) {
        locals.reset();
        locals.model = std::move(transform);
    });
    _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpIcon));
    _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
}

} // namespace game

} // namespace reone
