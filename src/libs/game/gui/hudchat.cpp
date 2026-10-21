/* Copyright (c) 2026 The reone project contributors */

// Co-op chat overlay and ping markers on the in-game HUD (coopchatui.h).
// Text and flat quads only, in the engine's HUD font, so the K1 and K2
// layouts share it and it carries no layout's art.

#include "reone/game/gui/hud.h"

#include "reone/game/coopsession.h"
#include "reone/game/game.h"
#include "reone/game/object/camera.h"
#include "reone/graphics/camera.h"
#include "reone/graphics/context.h"
#include "reone/graphics/font.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
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

void drawRect(ServicesView &services, glm::vec2 position, glm::vec2 size, glm::vec4 color) {
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

glm::vec3 colourOf(uint8_t index) {
    auto c = chatColour(index);
    return glm::vec3(c.r, c.g, c.b);
}

} // namespace

float HUD::chatOverlayBottom() const {
    // Above the lowest HUD controls on the left of the screen (K1: the
    // party/menu bar, K2: the action bar), so the line never covers them.
    const auto &options = _game.options().graphics;
    float width = static_cast<float>(options.width);
    float height = static_cast<float>(options.height);
    float bottom = height * 0.82f;
    auto &root = _gui->rootControl();
    const auto &offset = _gui->controlOffset();
    for (auto &child : root.children()) {
        auto &control = child.get();
        if (!control.isVisible()) {
            continue;
        }
        const auto &extent = control.extent();
        float left = static_cast<float>(offset.x + extent.left);
        float top = static_cast<float>(offset.y + extent.top);
        if (extent.width <= 0 || extent.height <= 0 || extent.width > width * 0.9f) {
            continue; // the full-screen frame, if any
        }
        if (left < width * 0.4f && top > height * 0.6f) {
            bottom = std::min(bottom, top);
        }
    }
    return bottom;
}

void HUD::renderCoopChat() {
    auto *coop = _game.coop();
    if (!coop) {
        return;
    }
    const auto &input = _game.chatInput();
    double now = coop->chatNow();
    auto lines = coop->chatLog().visible(now);
    auto pings = coop->pingBoard().active(now);
    if (lines.empty() && pings.empty() && !input.isOpen()) {
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
    float lineHeight = font.height() * scale * 1.2f;
    float pad = 6.0f * scale;
    float left = 12.0f * scale;
    float bottom = chatOverlayBottom() - pad;

    _services.graphics.uniforms.setGlobals([&options](auto &globals) {
        globals.reset();
        globals.projection = glm::ortho(0.0f, static_cast<float>(options.width), static_cast<float>(options.height), 0.0f, 0.0f, 100.0f);
        globals.projectionInv = glm::inverse(globals.projection);
    });

    // Pings: a marker and the sender (and object) over the point.
    std::shared_ptr<graphics::Camera> graphicsCamera;
    if (auto camera = _game.getActiveCamera()) {
        if (auto node = camera->cameraSceneNode()) {
            graphicsCamera = node->camera();
        }
    }

    _services.graphics.context.withBlendMode(BlendMode::Normal, [&]() {
        if (graphicsCamera) {
            glm::vec4 viewport(0.0f, 0.0f, 1.0f, 1.0f);
            for (const auto &shown : pings) {
                const auto &marker = *shown.marker;
                glm::vec3 position(marker.position.x, marker.position.y, marker.position.z);
                if (glm::dot(graphicsCamera->forward(), position - graphicsCamera->position()) <= 0.0f) {
                    continue; // behind the camera
                }
                glm::vec3 ground = glm::project(position, graphicsCamera->view(), graphicsCamera->projection(), viewport);
                glm::vec3 above = glm::project(position + glm::vec3(0.0f, 0.0f, 1.8f), graphicsCamera->view(), graphicsCamera->projection(), viewport);
                glm::vec2 base(ground.x * options.width, (1.0f - ground.y) * options.height);
                glm::vec2 top(above.x * options.width, (1.0f - above.y) * options.height);
                glm::vec3 colour = colourOf(marker.colour);
                float alpha = shown.alpha;
                // A pulsing ring on the ground (a square frame) and a stem up to the label.
                float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(now * 6.0));
                float ring = (10.0f + 4.0f * pulse) * scale;
                float t = 2.0f * scale;
                glm::vec4 c(colour, alpha);
                drawRect(_services, {base.x - ring, base.y - ring}, {2.0f * ring, t}, c);
                drawRect(_services, {base.x - ring, base.y + ring - t}, {2.0f * ring, t}, c);
                drawRect(_services, {base.x - ring, base.y - ring}, {t, 2.0f * ring}, c);
                drawRect(_services, {base.x + ring - t, base.y - ring}, {t, 2.0f * ring}, c);
                float stemTop = std::min(top.y, base.y - ring);
                drawRect(_services, {base.x - t * 0.5f, stemTop}, {t, std::max(0.0f, base.y - ring - stemTop)}, glm::vec4(colour, alpha * 0.8f));
                std::string label = marker.targetName.empty() ? marker.name : marker.targetName + "  (" + marker.name + ")";
                float labelWidth = font.measure(label, scale);
                // Keep the label on screen when the point is near an edge.
                float labelX = std::clamp(top.x, labelWidth * 0.5f + 2.0f * pad, static_cast<float>(options.width) - labelWidth * 0.5f - 2.0f * pad);
                drawRect(_services, {labelX - labelWidth * 0.5f - pad, stemTop - lineHeight - pad * 0.5f}, {labelWidth + 2.0f * pad, lineHeight},
                         glm::vec4(0.0f, 0.0f, 0.0f, 0.55f * alpha));
                font.render(label, glm::vec3(labelX, stemTop - lineHeight * 0.5f - pad * 0.5f, 0.0f), glm::vec4(colour, alpha), TextGravity::CenterCenter, scale);
            }
        }

        // The input line at the bottom, the messages above it.
        float y = bottom;
        if (input.isOpen()) {
            std::string prompt = "Say: ";
            std::string text = input.text() + "_";
            float width = std::max(360.0f * scale, font.measure(prompt + text, scale) + 2.0f * pad);
            drawRect(_services, {left - pad, y - lineHeight}, {width, lineHeight}, glm::vec4(0.0f, 0.0f, 0.0f, 0.65f));
            float baseline = y - lineHeight * 0.5f;
            font.render(prompt, glm::vec3(left, baseline, 0.0f), glm::vec4(0.75f, 0.85f, 1.0f, 1.0f), TextGravity::RightCenter, scale);
            font.render(text, glm::vec3(left + font.measure(prompt, scale), baseline, 0.0f), glm::vec4(1.0f), TextGravity::RightCenter, scale);
            y -= lineHeight + pad * 0.5f;
        }
        for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
            const auto &line = *it->line;
            float alpha = it->alpha;
            std::string name = line.name + (line.channel == net::ChatChannel::Team ? " [team]: " : ": ");
            float nameWidth = font.measure(name, scale);
            float textWidth = font.measure(line.text, scale);
            drawRect(_services, {left - pad, y - lineHeight}, {nameWidth + textWidth + 2.0f * pad, lineHeight}, glm::vec4(0.0f, 0.0f, 0.0f, 0.45f * alpha));
            float baseline = y - lineHeight * 0.5f;
            font.render(name, glm::vec3(left, baseline, 0.0f), glm::vec4(colourOf(line.colour), alpha), TextGravity::RightCenter, scale);
            font.render(line.text, glm::vec3(left + nameWidth, baseline, 0.0f), glm::vec4(0.95f, 0.95f, 0.9f, alpha), TextGravity::RightCenter, scale);
            y -= lineHeight;
        }
    });
}

} // namespace game

} // namespace reone
