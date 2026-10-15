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

#include "reone/scene/render/pipeline/retro.h"

#include "reone/graphics/context.h"
#include "reone/graphics/gputimer.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/pbrtextures.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/textureregistry.h"
#include "reone/graphics/textureutil.h"
#include "reone/graphics/uniforms.h"
#include "reone/scene/render/pass/retro.h"
#include "reone/system/checkutil.h"
#include "reone/system/threadutil.h"

using namespace reone::graphics;

namespace reone {

namespace scene {

// Must match kProbeFaceSize in pipeline.cpp (the probe target's height).
static constexpr int kProbeFaceSizeRetro = 256;

// Soft particles: a particle fades over the last 0.4 m before the surface
// behind it, so a flat sprite no longer shows a hard line where it cuts
// into a floor or wall.
static constexpr float kSoftParticleFade = 0.4f;

void RetroRenderPipeline::init() {
    checkThat(!_inited, "Pipeline already initialized");
    checkMainThread();

    initRenderTargets();
    initEnhancedTargets();

    _inited = true;
}

void RetroRenderPipeline::initRenderTargets() {
    // Tone mapping keeps the scene in half floats until the curve.
    const PixelFormat sceneFormat = hdrSceneTargets(_options) ? PixelFormat::RGBA16F : PixelFormat::RGBA8;
    // Shadows framebuffer

    _targets.dirLightShadowsDepth = std::make_shared<Texture>(
        "retro_dir_light_shadows_depth",
        TextureType::TwoDimArray,
        getTextureProperties(TextureUsage::DepthBuffer));
    _targets.dirLightShadowsDepth->clear(_options.shadowResolution, _options.shadowResolution, PixelFormat::Depth32F, kNumShadowCascades);
    _targets.dirLightShadowsDepth->init();

    _targets.pointLightShadowsDepth = std::make_shared<Texture>(
        "retro_point_light_shadows_depth",
        TextureType::CubeMap,
        getTextureProperties(TextureUsage::DepthBuffer));
    _targets.pointLightShadowsDepth->clear(_options.shadowResolution, _options.shadowResolution, PixelFormat::Depth32F, kNumCubeFaces);
    _targets.pointLightShadowsDepth->init();

    _targets.shadows = std::make_shared<Framebuffer>();
    // Always attach a depth buffer during initialization. Render
    // calls glClear(GL_DEPTH_BUFFER_BIT) before a depth buffer is
    // attached. If nothing is attached before glClear, some drivers
    // trip and never recover.
    _targets.shadows->attachDepth(_targets.pointLightShadowsDepth);
    _targets.shadows->init();

    // Opaque framebuffer

    _targets.opaqueColor = std::make_unique<Texture>(
        "retro_opaque_color",
        TextureType::TwoDim,
        getTextureProperties(TextureUsage::ColorBuffer));
    _targets.opaqueColor->clear(_targetSize.x, _targetSize.y, sceneFormat);
    _targets.opaqueColor->init();

    _targets.opaqueHilights = std::make_unique<Texture>(
        "retro_opaque_hilights",
        TextureType::TwoDim,
        getTextureProperties(TextureUsage::ColorBuffer));
    _targets.opaqueHilights->clear(_targetSize.x, _targetSize.y, PixelFormat::RGBA8);
    _targets.opaqueHilights->init();

    _targets.opaqueDepth = std::make_unique<Renderbuffer>();
    _targets.opaqueDepth->configure(_targetSize.x, _targetSize.y, PixelFormat::Depth24);
    _targets.opaqueDepth->init();

    _targets.opaque = std::make_unique<Framebuffer>();
    if (sceneDepthTextureNeeded(_options)) {
        // Enhanced effects read the opaque depth: a texture in place of the
        // renderbuffer, the same format, so the depth blits stay valid.
        _targets.opaqueDepthTexture = std::make_shared<Texture>(
            "retro_opaque_depth",
            TextureType::TwoDim,
            getTextureProperties(TextureUsage::DepthBuffer));
        _targets.opaqueDepthTexture->clear(_targetSize.x, _targetSize.y, PixelFormat::Depth24);
        _targets.opaqueDepthTexture->init();
        _targets.opaque->attachColorsDepth({_targets.opaqueColor, _targets.opaqueHilights}, _targets.opaqueDepthTexture);
    } else {
        _targets.opaque->attachColorsDepth({_targets.opaqueColor, _targets.opaqueHilights}, _targets.opaqueDepth);
    }
    _targets.opaque->init();

    // Transparent framebuffer

    _targets.transparentAccum = std::make_unique<Texture>(
        "retro_transparent_accum",
        TextureType::TwoDim,
        getTextureProperties(TextureUsage::ColorBuffer));
    _targets.transparentAccum->clear(_targetSize.x, _targetSize.y, PixelFormat::RGBA16F);
    _targets.transparentAccum->init();

    _targets.transparentRevealage = std::make_unique<Texture>(
        "retro_transparent_revealage",
        TextureType::TwoDim,
        getTextureProperties(TextureUsage::ColorBuffer));
    _targets.transparentRevealage->clear(_targetSize.x, _targetSize.y, PixelFormat::RGBA16F);
    _targets.transparentRevealage->init();

    _targets.transparentDepth = std::make_unique<Renderbuffer>();
    _targets.transparentDepth->configure(_targetSize.x, _targetSize.y, PixelFormat::Depth24);
    _targets.transparentDepth->init();

    _targets.transparent = std::make_unique<Framebuffer>();
    _targets.transparent->attachColorsDepth({_targets.transparentAccum,
                                             _targets.transparentRevealage},
                                            _targets.transparentDepth);
    _targets.transparent->init();

    // Ping-pong framebuffers

    _targets.pingColor = std::make_unique<Texture>(
        "retro_ping_color",
        TextureType::TwoDim,
        getTextureProperties(TextureUsage::ColorBuffer));
    _targets.pingColor->clear(_targetSize.x, _targetSize.y, sceneFormat);
    _targets.pingColor->init();

    _targets.pongColor = std::make_unique<Texture>(
        "retro_pong_color",
        TextureType::TwoDim,
        getTextureProperties(TextureUsage::ColorBuffer));
    _targets.pongColor->clear(_targetSize.x, _targetSize.y, sceneFormat);
    _targets.pongColor->init();

    _targets.pingPongDepth = std::make_unique<Renderbuffer>();
    _targets.pingPongDepth->configure(_targetSize.x, _targetSize.y, PixelFormat::Depth24);
    _targets.pingPongDepth->init();

    _targets.ping = std::make_unique<Framebuffer>();
    _targets.ping->attachColorDepth(_targets.pingColor, _targets.pingPongDepth);
    _targets.ping->init();

    _targets.pong = std::make_unique<Framebuffer>();
    _targets.pong->attachColorDepth(_targets.pongColor, _targets.pingPongDepth);
    _targets.pong->init();

    // Output framebuffer

    _targets.outputColor = std::make_unique<Texture>(
        "retro_output_color",
        TextureType::TwoDim,
        getTextureProperties(TextureUsage::ColorBuffer));
    _targets.outputColor->clear(_targetSize.x, _targetSize.y, sceneFormat);
    _targets.outputColor->init();

    _targets.outputDepth = std::make_unique<Renderbuffer>();
    _targets.outputDepth->configure(_targetSize.x, _targetSize.y, PixelFormat::Depth24);
    _targets.outputDepth->init();

    _targets.output = std::make_unique<Framebuffer>();
    _targets.output->attachColorDepth(_targets.outputColor, _targets.outputDepth);
    _targets.output->init();
}

void RetroRenderPipeline::storeProbeFace(int face) {
    if (!_probe.texture) {
        return;
    }
    glm::ivec4 src {0, 0, _targetSize};
    glm::ivec4 dst {face * kProbeFaceSizeRetro, 0, (face + 1) * kProbeFaceSizeRetro, kProbeFaceSizeRetro};
    _context.blitFramebuffer(*_targets.output, *_probe.framebuffer, src, dst, 0, 0,
                             FramebufferBlitFlags::color, FramebufferBlitFilter::Linear);
    _context.resetDrawFramebuffer();
    _context.resetReadFramebuffer();
}

Texture &RetroRenderPipeline::render() {
    auto pass = RetroRenderPass {_options,
                                 _context,
                                 _shaderRegistry,
                                 _statistic,
                                 _meshRegistry,
                                 _textureRegistry,
                                 _uniforms};

    gputimer::Scope pipelineTimer("pipeline");
    bool dirLightShadows = _passCallbacks.count(RenderPassName::DirLightShadowsPass) > 0;
    bool pointLightShadows = _passCallbacks.count(RenderPassName::PointLightShadows) > 0;
    if (dirLightShadows || pointLightShadows) {
        gputimer::Scope timer("shadows");
        _context.bindDrawFramebuffer(*_targets.shadows, {});
        _context.withViewport(glm::ivec4 {0, 0, _options.shadowResolution, _options.shadowResolution}, [this, &pass, &dirLightShadows]() {
            _context.clearDepth();
            if (dirLightShadows) {
                _targets.shadows->attachTexture(*_targets.dirLightShadowsDepth, Framebuffer::Attachment::Depth);
                _passCallbacks.at(RenderPassName::DirLightShadowsPass)(pass);
            } else {
                _targets.shadows->attachTexture(*_targets.pointLightShadowsDepth, Framebuffer::Attachment::Depth);
                _passCallbacks.at(RenderPassName::PointLightShadows)(pass);
            }
        });
        _context.checkErrors("shadow pass");
    }

    glm::ivec4 screenRect {0, 0, _targetSize};
    _context.withViewport(screenRect, [this, &pass, &dirLightShadows, &pointLightShadows, &screenRect]() {
        // Render opaque geometry
        gputimer::begin("opaque");
        _context.bindDrawFramebuffer(*_targets.opaque, {0, 1});
        _context.clearColorDepth();
        if (_passCallbacks.count(RenderPassName::OpaqueGeometry) > 0) {
            if (dirLightShadows) {
                _context.bindTexture(*_targets.dirLightShadowsDepth, TextureUnits::shadowMapArray);
            } else if (pointLightShadows) {
                _context.bindTexture(*_targets.pointLightShadowsDepth, TextureUnits::shadowMapCube);
            }
            _passCallbacks.at(RenderPassName::OpaqueGeometry)(pass);
        }
        _context.checkErrors("opaque pass");
        gputimer::end("opaque");

        // Ambient occlusion darkens the opaque colour before the glow and
        // the transparent geometry go on top.
        if (_options.gtao && _ao.raw.texture && _targets.opaqueDepthTexture) {
            gputimer::Scope timer("gtao");
            auto &ao = applyGTAO(*_targets.opaqueDepthTexture);
            _context.bindTexture(*_targets.opaqueColor);
            _context.bindTexture(ao, TextureUnits::ssao);
            drawFullscreen(ShaderProgramId::postAOApply, *_targets.ping, _targetSize);
            _context.blitFramebuffer(*_targets.ping, *_targets.opaque, screenRect, screenRect, 0, 0);
        }

        // One bounce of light from nearby surfaces, after AO and before the
        // glow and the transparent geometry.
        if (_options.ssgi && _gi.texture && _targets.opaqueDepthTexture) {
            gputimer::Scope timer("ssgi");
            applySSGI(*_targets.opaqueColor, *_targets.opaqueDepthTexture, *_targets.ping);
            _context.blitFramebuffer(*_targets.ping, *_targets.opaque, screenRect, screenRect, 0, 0);
        }

        // Reflections last of the opaque effects: they reflect the lit,
        // occluded colour; the pass also puts the alpha back to 1.
        if (_options.reflections && _targets.opaqueDepthTexture) {
            gputimer::Scope timer("ssr");
            applySSR(*_targets.opaqueColor, *_targets.opaqueDepthTexture, *_targets.ping);
            _context.blitFramebuffer(*_targets.ping, *_targets.opaque, screenRect, screenRect, 0, 0);
        }

        // Lightsaber blades also light the glow buffer, which is blurred
        // below and added under the transparent geometry: retail's
        // frame-buffer glow around a blade (design note p2_combat, item 10).
        // The transparent shader's first output is its colour times its
        // weight; added one-to-one it is the blade's own colour.
        if (_passCallbacks.count(RenderPassName::GlowGeometry) > 0) {
            _context.bindDrawFramebuffer(*_targets.opaque, {1});
            _context.withBlendMode(BlendMode::OIT_Transparent, [this, &pass]() {
                _context.withDepthMask(false, [this, &pass]() {
                    _passCallbacks.at(RenderPassName::GlowGeometry)(pass);
                });
            });
            _context.checkErrors("glow pass");
        }

        // Blur hilights
        gputimer::begin("glow");
        GaussianBlurParams blurParams;
        blurParams.vertical = false;
        blurParams.strong = true;
        applyGaussianBlur(
            *_targets.opaqueHilights,
            *_targets.ping,
            _targetSize,
            blurParams);
        blurParams.vertical = true;
        applyGaussianBlur(
            *_targets.pingColor,
            *_targets.pong,
            _targetSize,
            blurParams);
        _context.blitFramebuffer(
            *_targets.pong,
            *_targets.opaque,
            screenRect, screenRect,
            0, 1);
        gputimer::end("glow");

        // Render transparent geometry
        if (_passCallbacks.count(RenderPassName::TransparentGeometry) > 0) {
            gputimer::Scope timer("transparent");
            _context.blitFramebuffer(
                *_targets.opaque,
                *_targets.transparent,
                screenRect, screenRect,
                0, 0,
                FramebufferBlitFlags::depth);
            _context.bindDrawFramebuffer(*_targets.transparent, {0, 1});
            _context.clearColor({0.0f, 0.0f, 0.0f, 1.0f});
            // Soft particles read the opaque depth (a texture of its own,
            // not an attachment of the transparent target).
            bool softParticles = _options.softParticles && _targets.opaqueDepthTexture;
            if (softParticles) {
                _context.bindTexture(*_targets.opaqueDepthTexture, TextureUnits::gBufDepth);
            }
            _uniforms.setGlobals([softParticles](auto &globals) {
                globals.particleParams = glm::vec4(softParticles ? kSoftParticleFade : 0.0f, 0.0f, 0.0f, 0.0f);
            });
            _context.withBlendMode(BlendMode::OIT_Transparent, [this, &pass]() {
                _context.withDepthMask(false, [this, &pass]() {
                    _passCallbacks.at(RenderPassName::TransparentGeometry)(pass);
                });
            });
            _context.checkErrors("transparent pass");
        }

        // Blend opaque and transparent geometry
        gputimer::begin("composite");
        _context.bindDrawFramebuffer(*_targets.output, {0});
        _context.useProgram(_shaderRegistry.get(ShaderProgramId::oitBlend));
        _context.bindTexture(*_targets.opaqueColor, TextureUnits::mainTex);
        _context.bindTexture(*_targets.opaqueHilights, TextureUnits::hilights);
        _context.bindTexture(*_targets.transparentAccum, TextureUnits::oitAccum);
        _context.bindTexture(*_targets.transparentRevealage, TextureUnits::oitRevealage);
        _context.clearColorDepth();
        _meshRegistry.get(MeshName::quadNDC).draw(_statistic);
        gputimer::end("composite");

        // Volumetric light over the composed frame, before bloom and tone
        // mapping, so its glow blooms and rolls off with the rest.
        // Not in a GUI scene: its lights are authored for a lit backdrop (the
        // character screen's key light has multiplier 50), and scattered
        // through the haze they filled the panel with a flat glare.
        if (_options.volumetrics && !_guiScene && _volume.texture && _targets.opaqueDepthTexture) {
            gputimer::Scope timer("volumetrics");
            if (dirLightShadows) {
                _context.bindTexture(*_targets.dirLightShadowsDepth, TextureUnits::shadowMapArray);
            } else if (pointLightShadows) {
                _context.bindTexture(*_targets.pointLightShadowsDepth, TextureUnits::shadowMapCube);
            }
            int shadowType = dirLightShadows ? 1 : (pointLightShadows ? 2 : 0);
            applyVolumetrics(*_targets.outputColor, *_targets.opaqueDepthTexture, shadowType, *_targets.ping);
            _context.blitFramebuffer(*_targets.ping, *_targets.output, screenRect, screenRect, 0, 0);
        }

        if (_options.bloom && !_bloom.down.empty()) {
            gputimer::Scope timer("bloom");
            applyBloom(*_targets.outputColor, *_targets.opaqueHilights, *_targets.transparentRevealage, *_targets.ping);
            _context.blitFramebuffer(*_targets.ping, *_targets.output, screenRect, screenRect, 0, 0);
        }

        if (hdrSceneTargets(_options) && _tone.adapted[0].texture) {
            gputimer::Scope timer("tonemap");
            applyToneMapping(*_targets.outputColor, *_targets.ping);
            _context.blitFramebuffer(*_targets.ping, *_targets.output, screenRect, screenRect, 0, 0);
        }

        // Camera motion blur, on the display-referred frame (before DoF).
        if (_options.motionBlur && _targets.opaqueDepthTexture) {
            gputimer::Scope timer("motionblur");
            applyMotionBlur(*_targets.outputColor, *_targets.opaqueDepthTexture, *_targets.ping);
            _context.blitFramebuffer(*_targets.ping, *_targets.output, screenRect, screenRect, 0, 0);
        }

        // Conversation depth of field, on the display-referred frame.
        if (_options.dof && _options.dofAmount > 0.001f && _targets.opaqueDepthTexture) {
            gputimer::Scope timer("dof");
            applyDOF(*_targets.outputColor, *_targets.opaqueDepthTexture, *_targets.ping);
            _context.blitFramebuffer(*_targets.ping, *_targets.output, screenRect, screenRect, 0, 0);
        }

        // Colour grading on the display-referred frame, before AA.
        if (_options.grading) {
            gputimer::Scope timer("grading");
            if (applyGrading(*_targets.outputColor, *_targets.ping)) {
                _context.blitFramebuffer(*_targets.ping, *_targets.output, screenRect, screenRect, 0, 0);
            }
        }

        gputimer::begin("aa_sharpen");
        if (applyEnhancedAntiAliasing(_targets.opaqueDepthTexture.get(),
                                      *_targets.outputColor, *_targets.output,
                                      *_targets.pingColor, *_targets.ping,
                                      *_targets.pongColor, *_targets.pong)) {
            // SMAA or TAA, then sharpening
        } else if (_options.fxaa && _options.sharpen) {
            applyFXAA(*_targets.outputColor, *_targets.ping, _targetSize);
            applySharpen(*_targets.pingColor, *_targets.pong, _targetSize, _options.sharpenAmount);
            _context.blitFramebuffer(
                *_targets.pong,
                *_targets.output,
                screenRect, screenRect,
                0, 0);
        } else if (_options.fxaa || _options.sharpen) {
            if (_options.fxaa) {
                applyFXAA(*_targets.outputColor, *_targets.ping, _targetSize);
            } else if (_options.sharpen) {
                applySharpen(*_targets.outputColor, *_targets.ping, _targetSize, _options.sharpenAmount);
            }
            _context.blitFramebuffer(
                *_targets.ping,
                *_targets.output,
                screenRect, screenRect,
                0, 0);
        }
        gputimer::end("aa_sharpen");

        // Lens effects last, after anti-aliasing (grain must not be smoothed).
        if (lensEffectsOn()) {
            gputimer::Scope timer("lens");
            applyLensEffects(*_targets.outputColor, *_targets.ping);
            _context.blitFramebuffer(*_targets.ping, *_targets.output, screenRect, screenRect, 0, 0);
        }

        // Frame effects a power or a minigame names (speed blur, distortion
        // overlays): in every preset, they are game effects.
        applyFrameEffects(*_targets.output, *_targets.outputColor, *_targets.ping);

        // Post-processing effects
        if (_passCallbacks.count(RenderPassName::PostProcessing) > 0) {
            _passCallbacks.at(RenderPassName::PostProcessing)(pass);
        }
        _context.checkErrors("composite and post-processing");

        // Draw debug elements (lines, points, etc.)
        _context.bindDrawFramebuffer(*_targets.output, {0});
        _context.clearDepth();
        if (_passCallbacks.count(RenderPassName::Debug) > 0) {
            _passCallbacks.at(RenderPassName::Debug)(pass);
        }

        _context.resetDrawFramebuffer();
        _context.checkErrors("debug pass");
    });

    return *_targets.outputColor;
}

} // namespace scene

} // namespace reone
