/* Copyright (c) 2026 The reone project contributors */

// Co-op name tags above each player's character, and presentation-only HUD
// portraits for guest characters (stand-ins, Free Mode guests). Text and flat
// quads in the engine's HUD font plus the creature's own portrait texture, so
// the K1 and K2 layouts share it and it adds nothing to the party table.

#include "reone/game/gui/hud.h"

#include "reone/game/coopsession.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/camera.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/graphics/camera.h"
#include "reone/graphics/context.h"
#include "reone/graphics/font.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/uniforms.h"
#include "reone/resource/provider/fonts.h"
#include "reone/scene/node/camera.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

using namespace reone::graphics;

namespace reone {

namespace game {

namespace {

void fillRect(ServicesView &services, glm::vec2 position, glm::vec2 size, glm::vec4 color) {
    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, glm::vec3(position.x, position.y, 0.0f));
    transform = glm::scale(transform, glm::vec3(size.x, size.y, 1.0f));
    services.graphics.uniforms.setLocals([transform, color](auto &locals) {
        locals.reset();
        locals.model = transform;
        locals.color = color;
    });
    services.graphics.context.useProgram(services.graphics.shaderRegistry.get(ShaderProgramId::mvpColor));
    services.graphics.meshRegistry.get(MeshName::quad).draw(services.graphics.statistic);
}

glm::vec3 tint(uint8_t index) {
    auto c = chatColour(index);
    return glm::vec3(c.r, c.g, c.b);
}

} // namespace

bool HUD::coopOverlaysShown() const {
    auto *coop = _game.coop();
    if (!coop || !_game.coopNameTags()) {
        return false;
    }
    // Hidden in conversations, cutscenes (a scripted camera), movies and minigames.
    if (_game.currentScreen() != Game::Screen::InGame || _game.isMoviePlaying() || _game.miniGame()) {
        return false;
    }
    auto camera = _game.cameraType();
    return camera == CameraType::ThirdPerson || camera == CameraType::FirstPerson;
}

void HUD::renderCoopNameTags() {
    _coopPortraits.clear();
    if (!coopOverlaysShown()) {
        return;
    }
    auto *coop = _game.coop();
    auto presences = coop->presences();
    if (presences.empty()) {
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
    auto &font = *_guestListFont;
    float scale = std::min(options.width / 800.0f, options.height / 600.0f) * options.guiScale * options.guiTextScale;
    _services.graphics.uniforms.setGlobals([&options](auto &globals) {
        globals.reset();
        globals.projection = glm::ortho(0.0f, static_cast<float>(options.width), static_cast<float>(options.height), 0.0f, 0.0f, 100.0f);
        globals.projectionInv = glm::inverse(globals.projection);
    });

    std::shared_ptr<graphics::Camera> graphicsCamera;
    if (auto camera = _game.getActiveCamera()) {
        if (auto node = camera->cameraSceneNode()) {
            graphicsCamera = node->camera();
        }
    }
    _services.graphics.context.withBlendMode(BlendMode::Normal, [&]() {
        if (!graphicsCamera) {
            return;
        }
        glm::vec4 viewport(0.0f, 0.0f, 1.0f, 1.0f);
        float pad = 4.0f * scale;
        float lineHeight = font.height() * scale * 1.2f;
        for (const auto &presence : presences) {
            const auto &creature = *presence.object;
            if (!creature.visible() || creature.isDead()) {
                continue;
            }
            glm::vec3 position = creature.position() + glm::vec3(0.0f, 0.0f, 1.9f);
            if (glm::dot(graphicsCamera->forward(), position - graphicsCamera->position()) <= 0.0f ||
                glm::distance(graphicsCamera->position(), position) > 40.0f) {
                continue;
            }
            glm::vec3 p = glm::project(position, graphicsCamera->view(), graphicsCamera->projection(), viewport);
            float x = p.x * options.width;
            float y = (1.0f - p.y) * options.height;
            float width = font.measure(presence.name, scale);
            fillRect(_services, {x - width * 0.5f - pad, y - lineHeight * 0.5f}, {width + 2.0f * pad, lineHeight}, glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
            font.render(presence.name, glm::vec3(x, y, 0.0f), glm::vec4(tint(presence.colour), 1.0f), TextGravity::CenterCenter, scale);
        }
    });

    std::vector<PortraitSource> sources;
    for (const auto &presence : presences) {
        if (presence.outsideParty && !presence.own) {
            if (auto creature = std::dynamic_pointer_cast<Creature>(presence.object)) {
                sources.push_back({std::move(creature), presence.name, presence.colour});
            }
        }
    }
    renderCoopPortraits(sources, scale);
}

void HUD::renderCoopPortraits(const std::vector<PortraitSource> &sources, float scale) {
    const auto &options = _game.options().graphics;
    auto &font = *_guestListFont;
    float size = 52.0f * scale;
    float gap = 6.0f * scale;
    float right = static_cast<float>(options.width) - 12.0f * scale;
    float top = 12.0f * scale;
    if (_controls.LBL_MAPVIEW) {
        const auto &extent = _controls.LBL_MAPVIEW->extent();
        right = static_cast<float>(_gui->controlOffset().x + extent.left + extent.width);
        top = static_cast<float>(_gui->controlOffset().y + extent.top + extent.height) + 8.0f * scale;
        // The Free Mode guest list is drawn first.
        auto *coop = _game.coop();
        if (coop && coop->freeMode()) {
            top += static_cast<float>(coop->guestList().size()) * font.height() * scale * 1.15f;
        }
    }
    float x = right - size;
    float y = top;
    float labelHeight = font.height() * scale * 1.6f;
    for (const auto &presence : sources) {
        const auto &creature = presence.creature;
        _coopPortraits.push_back({creature, glm::vec4(x - 4.0f * scale, y - 4.0f * scale, size + 8.0f * scale, size + 12.0f * scale + labelHeight)});
        auto status = memberStatus(_services.resource.twoDas, *creature);
        float share = creature->maxHitPoints() > 0 ? std::clamp(static_cast<float>(creature->currentHitPoints()) / creature->maxHitPoints(), 0.0f, 1.0f) : 0.0f;
        bool selected = _game.module() && _game.module()->area() && _game.module()->area()->selectedObject() == creature;
        glm::vec3 colour = tint(presence.colour);
        _services.graphics.context.withBlendMode(BlendMode::Normal, [&]() {
            // Frame in the player's colour (white when selected), then the portrait.
            float frame = (selected ? 3.0f : 2.0f) * scale;
            fillRect(_services, {x - frame, y - frame}, {size + 2.0f * frame, size + 2.0f * frame}, selected ? glm::vec4(1.0f) : glm::vec4(colour, 1.0f));
            fillRect(_services, {x, y}, {size, size}, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            if (auto portrait = creature->portrait()) {
                _services.graphics.context.bindTexture(*portrait);
                glm::mat4 transform(1.0f);
                transform = glm::translate(transform, glm::vec3(x, y, 0.0f));
                transform = glm::scale(transform, glm::vec3(size, size, 1.0f));
                _services.graphics.uniforms.setLocals([transform](auto &locals) {
                    locals.reset();
                    locals.model = transform;
                });
                _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpTexture));
                _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
            }
            // Status markers: down darkens it red; chips for cannot-act, effects and combat.
            if (status.down) {
                fillRect(_services, {x, y}, {size, size}, glm::vec4(0.6f, 0.0f, 0.0f, 0.55f));
            }
            float chip = 8.0f * scale;
            if (status.debilitated) {
                fillRect(_services, {x, y}, {chip, chip}, glm::vec4(1.0f, 0.85f, 0.2f, 1.0f));
            }
            if (status.helped) {
                fillRect(_services, {x + size - chip, y}, {chip, chip}, glm::vec4(0.3f, 0.8f, 1.0f, 1.0f));
            }
            if (status.hindered) {
                fillRect(_services, {x + size - chip, y + chip + scale}, {chip, chip}, glm::vec4(1.0f, 0.3f, 0.3f, 1.0f));
            }
            if (creature->isInCombat()) {
                fillRect(_services, {x, y + size - chip}, {chip, chip}, glm::vec4(1.0f, 0.5f, 0.1f, 1.0f));
            }
            // Health bar under it, then the name.
            float barTop = y + size + 2.0f * scale;
            float barHeight = 5.0f * scale;
            glm::vec4 bar = share > 0.5f ? glm::vec4(0.3f, 0.85f, 0.3f, 1.0f) : share > 0.25f ? glm::vec4(0.95f, 0.8f, 0.2f, 1.0f) : glm::vec4(0.9f, 0.25f, 0.2f, 1.0f);
            fillRect(_services, {x, barTop}, {size, barHeight}, glm::vec4(0.0f, 0.0f, 0.0f, 0.7f));
            fillRect(_services, {x, barTop}, {size * share, barHeight}, bar);
            font.render(presence.name, glm::vec3(x + size * 0.5f, barTop + barHeight + font.height() * scale * 0.7f, 0.0f), glm::vec4(colour, 1.0f),
                        TextGravity::CenterCenter, scale);
        });
        y += size + gap + 5.0f * scale + labelHeight;
    }
}

bool HUD::handleCoopPortraitClick(const input::Event &event) {
    if (event.type != input::EventType::MouseButtonDown || event.button.button != input::MouseButton::Left || _coopPortraits.empty()) {
        return false;
    }
    auto module = _game.module();
    if (!module || !module->area()) {
        return false;
    }
    for (const auto &entry : _coopPortraits) {
        const auto &r = entry.rect;
        if (event.button.x >= r.x && event.button.x <= r.x + r.z && event.button.y >= r.y && event.button.y <= r.y + r.w) {
            // Selecting it targets it like any creature clicked in the world.
            module->area()->selectObject(entry.creature);
            return true;
        }
    }
    return false;
}

} // namespace game

} // namespace reone
