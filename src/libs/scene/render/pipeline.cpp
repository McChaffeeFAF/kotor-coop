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

#include "reone/scene/render/pipeline.h"

#include "reone/graphics/context.h"
#include "reone/graphics/gputimer.h"
#include "reone/graphics/grading.h"
#include "reone/graphics/preset.h"
#include "reone/graphics/textureutil.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/pbrtextures.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/statistic.h"
#include "reone/graphics/textureregistry.h"
#include "reone/graphics/uniforms.h"
#include "reone/scene/render/pipeline/pbr.h"
#include "reone/scene/render/pipeline/retro.h"

using namespace reone::graphics;

namespace reone {

namespace scene {

std::string renderPipelineKey(const GraphicsOptions &options) {
    std::string key;
    key += options.pbr ? 'P' : 'R';
    key += options.ssao ? 'O' : '-';
    key += options.ssr ? 'S' : '-';
    key += options.gtao ? 'G' : '-';
    key += options.bloom ? 'B' : '-';
    key += options.smaa ? 'M' : '-';
    key += options.taa ? 'T' : '-';
    key += options.reshade ? 'D' : '-';
    key += hdrSceneTargets(options) ? 'H' : '-';
    key += options.volumetrics ? 'V' : '-';
    key += options.softParticles ? 'Z' : '-';
    key += options.ssgi ? 'I' : '-';
    key += options.reflections ? 'F' : '-';
    key += options.dof ? 'C' : '-';
    key += options.motionBlur ? 'M' : '-';
    key += std::to_string(options.shadowResolution);
    return key;
}

bool hdrSceneTargets(const GraphicsOptions &options) {
    return options.tonemap || options.autoExposure;
}

bool sceneDepthTextureNeeded(const GraphicsOptions &options) {
    return options.taa || options.gtao || options.reshade || options.volumetrics || options.softParticles || options.ssgi || options.reflections || options.dof || options.motionBlur;
}

void RenderPipelineBase::applyBoxBlur(Texture &srcTexture, Framebuffer &dst, const glm::ivec2 &size) {
    _context.useProgram(_shaderRegistry.get(ShaderProgramId::postBoxBlur4));
    _context.bindDrawFramebuffer(dst, {0});
    _context.bindTexture(srcTexture);
    _context.withViewport(glm::ivec4(0, 0, size), [this]() {
        _context.clearColorDepth();
        _meshRegistry.get(MeshName::quadNDC).draw(_statistic);
    });
}

void RenderPipelineBase::applyGaussianBlur(Texture &tex,
                                           Framebuffer &dst,
                                           const glm::ivec2 &size,
                                           const GaussianBlurParams &params) {
    _uniforms.setScreenEffect([&size, &params](auto &se) {
        se.screenResolution = glm::vec2(size);
        se.screenResolutionRcp = 1.0f / se.screenResolution;
        se.blurDirection = params.vertical ? glm::vec2(0.0f, 1.0f) : glm::vec2(1.0f, 0.0f);
    });
    _context.useProgram(_shaderRegistry.get(params.strong
                                                ? ShaderProgramId::postGaussianBlur13
                                                : ShaderProgramId::postGaussianBlur9));
    _context.bindDrawFramebuffer(dst, {0});
    _context.bindTexture(tex);
    _context.withViewport(glm::ivec4(0, 0, size), [this]() {
        _context.clearColorDepth();
        _meshRegistry.get(MeshName::quadNDC).draw(_statistic);
    });
}

void RenderPipelineBase::applyMedianFilter(Texture &tex,
                                           Framebuffer &dst,
                                           const glm::ivec2 &size,
                                           bool strong) {
    _context.useProgram(_shaderRegistry.get(strong
                                                ? ShaderProgramId::postMedianFilter5
                                                : ShaderProgramId::postMedianFilter3));
    _context.bindDrawFramebuffer(dst, {0});
    _context.bindTexture(tex);
    _context.withViewport(glm::ivec4(0, 0, size), [this]() {
        _context.clearColorDepth();
        _meshRegistry.get(MeshName::quadNDC).draw(_statistic);
    });
}

void RenderPipelineBase::applyFXAA(Texture &tex, Framebuffer &dst, const glm::ivec2 &size) {
    _uniforms.setScreenEffect([&size](auto &se) {
        se.screenResolution = glm::vec2(size);
        se.screenResolutionRcp = 1.0f / se.screenResolution;
    });
    _context.useProgram(_shaderRegistry.get(ShaderProgramId::postFXAA));
    _context.bindDrawFramebuffer(dst, {0});
    _context.bindTexture(tex);
    _context.withViewport(glm::ivec4(0, 0, size), [this]() {
        _context.clearColorDepth();
        _meshRegistry.get(MeshName::quadNDC).draw(_statistic);
    });
}

void RenderPipelineBase::applySharpen(Texture &tex,
                                      Framebuffer &dst,
                                      const glm::ivec2 &size,
                                      float amount) {
    _uniforms.setScreenEffect([&size, &amount](auto &se) {
        se.screenResolution = glm::vec2(size);
        se.screenResolutionRcp = 1.0f / se.screenResolution;
        se.sharpenAmount = amount;
    });
    _context.useProgram(_shaderRegistry.get(ShaderProgramId::postSharpen));
    _context.bindDrawFramebuffer(dst, {0});
    _context.bindTexture(tex);
    _context.withViewport(glm::ivec4(0, 0, size), [this]() {
        _context.clearColorDepth();
        _meshRegistry.get(MeshName::quadNDC).draw(_statistic);
    });
}

// Bloom tuning: a soft knee around a high threshold, so only the brightest
// parts of the LDR frame and the glow channel spread.
static constexpr float kBloomThreshold = 0.75f;
static constexpr float kBloomKnee = 0.25f;
static constexpr float kBloomGlowWeight = 1.0f;
static constexpr int kMaxBloomLevels = 6;

// TAA: 8 Halton (2, 3) jitter positions; 0.9 of the history kept per frame;
// a camera jump beyond these limits starts the history again (a cut).
static constexpr int kTAASamples = 8;
static constexpr float kTAAHistoryWeight = 0.9f;
static constexpr float kTAACutDistance = 2.0f;
static constexpr float kTAACutCosine = 0.85f;

// GTAO: a 1 m radius (KotOR units are metres) fading over its outer 40%;
// strength is how far the retro look's colour is scaled down by it.
static constexpr float kGTAORadius = 1.0f;
static constexpr float kGTAOFalloff = 0.4f;
static constexpr float kGTAOStrength = 0.85f;

// Eye adaptation: the frame is metered over 32x18 tiles (centre-weighted
// log-average of linear luma); the exposure that brings it to the target
// is clamped to [-0.75, +1] EV and eased toward, faster into the light.
static constexpr int kExposureMeterWidth = 32;
static constexpr int kExposureMeterHeight = 18;
static constexpr float kExposureTarget = 0.10f;
static constexpr float kExposureMinEV = -0.75f;
static constexpr float kExposureMaxEV = 1.0f;
static constexpr float kExposureSpeedBrighten = 1.2f;
static constexpr float kExposureSpeedDarken = 2.5f;

// Volumetric light: 20 steps over at most 40 m of a thin haze; an area with
// fog gets a denser medium in the fog's colour (density from the distance
// at which the retail fog is total). Intensity scales the in-scattered light.
static constexpr int kVolumeSteps = 20;
static constexpr float kVolumeMaxDistance = 40.0f;
static constexpr float kVolumeHazeDensity = 0.012f;
static constexpr float kVolumeFogDensityScale = 1.5f; // density = scale / fog far
static constexpr float kVolumeMinFogDensity = 0.008f;
static constexpr float kVolumeMaxFogDensity = 0.06f;
static constexpr float kVolumeIntensity = 0.35f;
static constexpr float kVolumePhaseG = 0.35f;
static constexpr float kVolumeAmbientWeight = 0.15f;

// SSGI: 12 samples within 1.5 m, at half resolution; the bounce is added
// at this strength (the surface's own colour stands in for its albedo).
static constexpr float kSSGIRadius = 1.5f;
static constexpr float kSSGIIntensity = 0.8f;

// Reflections: SSR strength and reach; probe faces of 256 texels.
static constexpr float kSSRStrength = 0.85f;
static constexpr float kSSRMaxDistance = 18.0f;
static constexpr int kProbeFaceSize = 256;

glm::vec3 probeFaceDirection(int face) {
    static const glm::vec3 dirs[6] {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    return dirs[glm::clamp(face, 0, 5)];
}

glm::vec3 probeFaceUp(int face) {
    static const glm::vec3 ups[6] {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}};
    return ups[glm::clamp(face, 0, 5)];
}

static float halton(uint32_t index, uint32_t base) {
    float f = 1.0f;
    float r = 0.0f;
    while (index > 0) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(index % base);
        index /= base;
    }
    return r;
}

RenderPipelineBase::ColorTarget RenderPipelineBase::makeColorTarget(const std::string &name, glm::ivec2 size, PixelFormat format) {
    ColorTarget target;
    target.size = glm::max(size, glm::ivec2(1));
    target.texture = std::make_shared<Texture>(name, TextureType::TwoDim, getTextureProperties(TextureUsage::ColorBuffer));
    target.texture->clear(target.size.x, target.size.y, format);
    target.texture->init();
    target.framebuffer = std::make_shared<Framebuffer>();
    target.framebuffer->attachColorDepth(target.texture, nullptr);
    target.framebuffer->init();
    return target;
}

void RenderPipelineBase::initEnhancedTargets() {
    if (_options.bloom) {
        glm::ivec2 size = _targetSize / 2;
        for (int i = 0; i < kMaxBloomLevels && size.x >= 4 && size.y >= 4; ++i) {
            _bloom.down.push_back(makeColorTarget("bloom_down" + std::to_string(i), size, PixelFormat::RGBA16F));
            size /= 2;
        }
        for (size_t i = 0; i + 1 < _bloom.down.size(); ++i) {
            _bloom.up.push_back(makeColorTarget("bloom_up" + std::to_string(i), _bloom.down[i].size, PixelFormat::RGBA16F));
        }
    }
    if (_options.taa) {
        for (int i = 0; i < 2; ++i) {
            _taa.history[i] = makeColorTarget("taa_history" + std::to_string(i), _targetSize, PixelFormat::RGBA16F);
        }
    }
    if (_options.gtao) {
        _ao.raw = makeColorTarget("gtao_raw", _targetSize, PixelFormat::RG16F);
        _ao.blur = makeColorTarget("gtao_blur", _targetSize, PixelFormat::RG16F);
    }
    if (hdrSceneTargets(_options)) {
        _tone.meter = makeColorTarget("exposure_meter", glm::ivec2(kExposureMeterWidth, kExposureMeterHeight), PixelFormat::RG16F);
        for (int i = 0; i < 2; ++i) {
            _tone.adapted[i] = makeColorTarget("exposure_adapted" + std::to_string(i), glm::ivec2(1), PixelFormat::RGBA16F);
        }
    }
    if (_options.reflections) {
        _probe = makeColorTarget("reflection_probe", glm::ivec2(6 * kProbeFaceSize, kProbeFaceSize), PixelFormat::RGBA16F);
    }
    if (_options.ssgi) {
        _gi = makeColorTarget("ssgi", glm::max(_targetSize / 2, glm::ivec2(1)), PixelFormat::RGBA16F);
    }
    if (_options.volumetrics) {
        _volume = makeColorTarget("volume", glm::max(_targetSize / 2, glm::ivec2(1)), PixelFormat::RGBA16F);
    }
    if (_options.smaa) {
        _smaa.edges = makeColorTarget("smaa_edges", _targetSize, PixelFormat::RG8);
        _smaa.weights = makeColorTarget("smaa_weights", _targetSize, PixelFormat::RGBA8);
    }
}

void RenderPipelineBase::applyToneMapping(Texture &src, Framebuffer &dst) {
    // A GUI scene keeps the fixed exposure: metering the character screen's
    // dark, mostly empty frame brightened it until its faint mist was a glare.
    bool autoExposure = _options.autoExposure && !_guiScene && _tone.meter.texture;
    float op = _options.tonemap ? static_cast<float>(_options.tonemapOperator) : 0.0f;
    float dt = glm::clamp(_frameTime, 0.0f, 0.25f);
    bool valid = _tone.valid;
    _uniforms.setScreenEffect([&](auto &se) {
        se.screenResolution = glm::vec2(_targetSize);
        se.screenResolutionRcp = 1.0f / se.screenResolution;
        se.toneParams = glm::vec4(op, autoExposure ? 1.0f : 0.0f, 0.0f, kExposureTarget);
        se.exposureParams = glm::vec4(dt, kExposureSpeedBrighten, kExposureSpeedDarken, valid ? 1.0f : 0.0f);
        se.exposureRange = glm::vec4(kExposureMinEV, kExposureMaxEV, kExposureMeterWidth, kExposureMeterHeight);
    });
    if (autoExposure) {
        gputimer::Scope timer("exposure");
        _context.bindTexture(src);
        drawFullscreen(ShaderProgramId::postLumMeter, *_tone.meter.framebuffer, _tone.meter.size);
        auto &previous = _tone.adapted[_tone.current];
        auto &next = _tone.adapted[1 - _tone.current];
        _context.bindTexture(*_tone.meter.texture);
        _context.bindTexture(*previous.texture, TextureUnits::hilights);
        drawFullscreen(ShaderProgramId::postExposure, *next.framebuffer, next.size);
        _tone.current = 1 - _tone.current;
        _tone.valid = true;
    }
    _context.bindTexture(src);
    if (_tone.adapted[_tone.current].texture) {
        _context.bindTexture(*_tone.adapted[_tone.current].texture, TextureUnits::hilights);
    }
    drawFullscreen(ShaderProgramId::postToneMap, dst, _targetSize);
}

void RenderPipelineBase::applyVolumetrics(Texture &scene, Texture &depth, int shadowType, Framebuffer &dst) {
    // The medium: the area's fog where it has one, else a faint neutral haze.
    float density = kVolumeHazeDensity;
    glm::vec3 tint(1.0f);
    _uniforms.setGlobals([&](auto &globals) {
        if (globals.fogFar > globals.fogNear && globals.fogFar > 0.0f) {
            density = glm::clamp(kVolumeFogDensityScale / globals.fogFar, kVolumeMinFogDensity, kVolumeMaxFogDensity);
            glm::vec3 fog(globals.fogColor);
            float peak = glm::max(fog.r, glm::max(fog.g, fog.b));
            tint = peak > 0.0f ? glm::mix(glm::vec3(1.0f), fog / peak, 0.6f) : glm::vec3(1.0f);
        }
        globals.volumeParams = glm::vec4(density, kVolumeIntensity, kVolumePhaseG, kVolumeMaxDistance);
        globals.volumeTint = glm::vec4(tint, static_cast<float>(kVolumeSteps));
        globals.volumeShadow = glm::vec4(static_cast<float>(shadowType), static_cast<float>(_volumeFrame % 64), kVolumeAmbientWeight, 0.0f);
    });
    ++_volumeFrame;
    _context.bindTexture(depth, TextureUnits::gBufDepth);
    drawFullscreen(ShaderProgramId::postVolumetric, *_volume.framebuffer, _volume.size);
    _context.bindTexture(scene);
    _context.bindTexture(*_volume.texture, TextureUnits::hilights);
    _context.bindTexture(depth, TextureUnits::gBufDepth);
    drawFullscreen(ShaderProgramId::postVolumeComposite, dst, _targetSize);
}

void RenderPipelineBase::applySSGI(Texture &opaque, Texture &depth, Framebuffer &dst) {
    float noiseFrame = static_cast<float>(_giFrame++ % 64);
    _uniforms.setScreenEffect([&](auto &se) {
        se.screenResolution = glm::vec2(_targetSize);
        se.screenResolutionRcp = 1.0f / se.screenResolution;
        se.giParams = glm::vec4(kSSGIRadius, kSSGIIntensity, noiseFrame, 0.0f);
    });
    _context.bindTexture(opaque);
    _context.bindTexture(depth, TextureUnits::gBufDepth);
    drawFullscreen(ShaderProgramId::postSSGI, *_gi.framebuffer, _gi.size);
    _context.bindTexture(opaque);
    _context.bindTexture(*_gi.texture, TextureUnits::hilights);
    _context.bindTexture(depth, TextureUnits::gBufDepth);
    drawFullscreen(ShaderProgramId::postSSGIApply, dst, _targetSize);
}

void RenderPipelineBase::applySSR(Texture &opaque, Texture &depth, Framebuffer &dst) {
    float noiseFrame = static_cast<float>(_ssrFrame++ % 64);
    glm::mat4 viewInv = glm::inverse(_view);
    bool probe = _probeValid && _probe.texture;
    _uniforms.setScreenEffect([&](auto &se) {
        se.screenResolution = glm::vec2(_targetSize);
        se.screenResolutionRcp = 1.0f / se.screenResolution;
        se.ssrParams = glm::vec4(kSSRStrength, kSSRMaxDistance, probe ? 1.0f : 0.0f, noiseFrame);
        se.ssrViewInv = viewInv;
    });
    _context.bindTexture(opaque);
    _context.bindTexture(depth, TextureUnits::gBufDepth);
    if (probe) {
        _context.bindTexture(*_probe.texture, TextureUnits::envMap);
    }
    drawFullscreen(ShaderProgramId::postSSR, dst, _targetSize);
}

// Conversation depth of field: blur up to 1/120 of the screen height; the
// in-focus depth on each side of the speaker grows with the distance.
static constexpr float kDOFMaxRadiusFraction = 1.0f / 120.0f;
static constexpr float kDOFFocusRangeBase = 0.6f;
static constexpr float kDOFFocusRangeScale = 0.15f;

void RenderPipelineBase::applyDOF(Texture &src, Texture &depth, Framebuffer &dst) {
    float radius = kDOFMaxRadiusFraction * static_cast<float>(_targetSize.y);
    float range = kDOFFocusRangeBase + kDOFFocusRangeScale * _options.dofFocus;
    _uniforms.setScreenEffect([&](auto &se) {
        se.screenResolution = glm::vec2(_targetSize);
        se.screenResolutionRcp = 1.0f / se.screenResolution;
        se.dofParams = glm::vec4(_options.dofFocus, glm::clamp(_options.dofAmount, 0.0f, 1.0f), radius, range);
    });
    _context.bindTexture(src);
    _context.bindTexture(depth, TextureUnits::gBufDepth);
    drawFullscreen(ShaderProgramId::postDOF, dst, _targetSize);
}

// Motion blur: half the frame's motion (a 180-degree shutter), at most 1/60
// of the screen width; a camera jump of over 2 m is a cut (no blur).
static constexpr float kMotionBlurShutter = 0.5f;
static constexpr float kMotionBlurMaxFraction = 1.0f / 60.0f;
static constexpr float kMotionBlurCutDistance = 2.0f;

void RenderPipelineBase::applyMotionBlur(Texture &src, Texture &depth, Framebuffer &dst) {
    bool valid = _motion.valid &&
                 glm::distance(_motion.cameraPosition, _motion.previousCameraPosition) < kMotionBlurCutDistance;
    glm::mat4 reprojection = _motion.previousViewProjection * glm::inverse(_motion.viewProjection);
    _uniforms.setScreenEffect([&](auto &se) {
        se.screenResolution = glm::vec2(_targetSize);
        se.screenResolutionRcp = 1.0f / se.screenResolution;
        se.reprojection = reprojection;
        se.motionParams = glm::vec4(kMotionBlurShutter, kMotionBlurMaxFraction * static_cast<float>(_targetSize.x), valid ? 1.0f : 0.0f, 0.0f);
    });
    _context.bindTexture(src);
    _context.bindTexture(depth, TextureUnits::gBufDepth);
    drawFullscreen(ShaderProgramId::postMotionBlur, dst, _targetSize);
    _motion.previousViewProjection = _motion.viewProjection;
    _motion.previousCameraPosition = _motion.cameraPosition;
    _motion.valid = true;
}

bool RenderPipelineBase::lensEffectsOn() const {
    return _options.lensFlares || _options.filmGrain || _options.vignette || _options.chromaticAberration;
}

void RenderPipelineBase::applyLensEffects(Texture &src, Framebuffer &dst) {
    float noiseFrame = static_cast<float>(_lensFrame++ % 1024);
    _uniforms.setScreenEffect([&](auto &se) {
        se.screenResolution = glm::vec2(_targetSize);
        se.screenResolutionRcp = 1.0f / se.screenResolution;
        se.lensFlags = glm::vec4(_options.lensFlares ? 1.0f : 0.0f, _options.filmGrain ? 1.0f : 0.0f,
                                 _options.vignette ? 1.0f : 0.0f, _options.chromaticAberration ? 1.0f : 0.0f);
        se.lensParams = glm::vec4(noiseFrame, 0.0f, 0.0f, 0.0f);
    });
    _context.bindTexture(src);
    drawFullscreen(ShaderProgramId::postLens, dst, _targetSize);
}

std::string RenderPipelineBase::activeGradingProfile() const {
    if (_options.gradingOverride == "neutral") {
        return "";
    }
    if (_options.gradingOverride != "auto" && !_options.gradingOverride.empty()) {
        return _options.gradingOverride;
    }
    return _options.gradingProfile;
}

bool RenderPipelineBase::applyGrading(Texture &src, Framebuffer &dst) {
    std::string profile = activeGradingProfile();
    const ColorGrade *grade = colorGrade(profile);
    if (!grade) {
        return false;
    }
    if (!_gradeLUT || _gradeLUTProfile != profile) {
        auto pixels = bakeColorGradeLUT(*grade);
        auto buffer = std::make_shared<ByteBuffer>(pixels.begin(), pixels.end());
        _gradeLUT = std::make_shared<Texture>("grade_lut", TextureType::TwoDim, getTextureProperties(TextureUsage::ColorBuffer));
        _gradeLUT->setPixels(kGradeLUTSize * kGradeLUTSize, kGradeLUTSize, PixelFormat::RGB8, Texture::Layer {buffer});
        _gradeLUT->init();
        _gradeLUTProfile = profile;
    }
    _context.bindTexture(src);
    _context.bindTexture(*_gradeLUT, TextureUnits::hilights);
    drawFullscreen(ShaderProgramId::postGrade, dst, _targetSize);
    return true;
}

void RenderPipelineBase::setCamera(const glm::mat4 &projection, const glm::mat4 &view) {
    _view = view;
    _motion.viewProjection = projection * view;
    _motion.cameraPosition = glm::vec3(glm::inverse(view)[3]);
    if (!_options.taa || !_taa.history[0].texture) {
        _taa.jitter = glm::vec2(0.0f);
        return;
    }
    _taa.viewProjection = projection * view;
    glm::mat4 viewInv = glm::inverse(view);
    _taa.cameraPosition = glm::vec3(viewInv[3]);
    _taa.cameraForward = -glm::normalize(glm::vec3(viewInv[2]));
    uint32_t index = (_taa.frame % kTAASamples) + 1;
    _taa.jitter = glm::vec2(halton(index, 2) - 0.5f, halton(index, 3) - 0.5f);
}

void RenderPipelineBase::applyTAA(Texture &src, Texture &depth, Framebuffer &dst) {
    bool cut = glm::distance(_taa.cameraPosition, _taa.previousCameraPosition) > kTAACutDistance ||
               glm::dot(_taa.cameraForward, _taa.previousCameraForward) < kTAACutCosine;
    bool valid = _taa.valid && !cut;
    glm::mat4 reprojection = _taa.previousViewProjection * glm::inverse(_taa.viewProjection);
    _uniforms.setScreenEffect([&](auto &se) {
        se.screenResolution = glm::vec2(_targetSize);
        se.screenResolutionRcp = 1.0f / se.screenResolution;
        se.taaParams = glm::vec4(kTAAHistoryWeight, valid ? 1.0f : 0.0f, 0.0f, 0.0f);
        se.reprojection = reprojection;
    });
    auto &previous = _taa.history[_taa.current];
    auto &next = _taa.history[1 - _taa.current];
    _context.bindTexture(src);
    _context.bindTexture(*previous.texture, TextureUnits::hilights);
    _context.bindTexture(depth, TextureUnits::gBufDepth);
    drawFullscreen(ShaderProgramId::postTAA, *next.framebuffer, _targetSize);
    glm::ivec4 rect {0, 0, _targetSize};
    _context.blitFramebuffer(*next.framebuffer, dst, rect, rect, 0, 0);

    _taa.current = 1 - _taa.current;
    _taa.valid = true;
    _taa.previousViewProjection = _taa.viewProjection;
    _taa.previousCameraPosition = _taa.cameraPosition;
    _taa.previousCameraForward = _taa.cameraForward;
    ++_taa.frame;
}

Texture &RenderPipelineBase::applyGTAO(Texture &depth) {
    float noiseFrame = temporalNoiseFrame();
    _uniforms.setScreenEffect([&](auto &se) {
        se.screenResolution = glm::vec2(_targetSize);
        se.screenResolutionRcp = 1.0f / se.screenResolution;
        se.aoParams = glm::vec4(kGTAORadius, kGTAOFalloff, noiseFrame, kGTAOStrength);
    });
    _context.bindTexture(depth, TextureUnits::gBufDepth);
    drawFullscreen(ShaderProgramId::postGTAO, *_ao.raw.framebuffer, _targetSize);
    for (int pass = 0; pass < 2; ++pass) {
        bool vertical = pass == 1;
        _uniforms.setScreenEffect([&](auto &se) {
            se.blurDirection = vertical ? glm::vec2(0.0f, 1.0f) : glm::vec2(1.0f, 0.0f);
        });
        _context.bindTexture(vertical ? *_ao.blur.texture : *_ao.raw.texture);
        drawFullscreen(ShaderProgramId::postAOBlur, vertical ? *_ao.raw.framebuffer : *_ao.blur.framebuffer, _targetSize);
    }
    return *_ao.raw.texture;
}

void RenderPipelineBase::applySMAA(Texture &src, Framebuffer &dst) {
    _uniforms.setScreenEffect([this](auto &se) {
        se.screenResolution = glm::vec2(_targetSize);
        se.screenResolutionRcp = 1.0f / se.screenResolution;
    });
    _context.bindTexture(src);
    drawFullscreen(ShaderProgramId::postSMAAEdge, *_smaa.edges.framebuffer, _targetSize);
    _context.bindTexture(*_smaa.edges.texture);
    drawFullscreen(ShaderProgramId::postSMAAWeights, *_smaa.weights.framebuffer, _targetSize);
    _context.bindTexture(src);
    _context.bindTexture(*_smaa.weights.texture, TextureUnits::hilights);
    drawFullscreen(ShaderProgramId::postSMAABlend, dst, _targetSize);
}

bool RenderPipelineBase::applyEnhancedAntiAliasing(Texture *depth,
                                                   Texture &outputColor,
                                                   Framebuffer &output,
                                                   Texture &pingColor,
                                                   Framebuffer &ping,
                                                   Texture &pongColor,
                                                   Framebuffer &pong) {
    auto mode = antiAliasing(_options);
    bool taa = mode == AntiAliasing::TAA && _taa.history[0].texture && depth;
    bool smaa = mode == AntiAliasing::SMAA && _smaa.edges.texture;
    if (!taa && !smaa) {
        return false; // the retail chain runs
    }
    glm::ivec4 rect {0, 0, _targetSize};
    if (taa) {
        gputimer::Scope timer("taa");
        applyTAA(outputColor, *depth, ping);
    } else {
        gputimer::Scope timer("smaa");
        applySMAA(outputColor, ping);
    }
    if (_options.sharpen) {
        gputimer::Scope timer("sharpen");
        applySharpen(pingColor, pong, _targetSize, _options.sharpenAmount);
        _context.blitFramebuffer(pong, output, rect, rect, 0, 0);
    } else {
        _context.blitFramebuffer(ping, output, rect, rect, 0, 0);
    }
    return true;
}

void RenderPipelineBase::drawFullscreen(const std::string &program, Framebuffer &dst, const glm::ivec2 &size) {
    _context.useProgram(_shaderRegistry.get(program));
    _context.bindDrawFramebuffer(dst, {0});
    _context.withViewport(glm::ivec4(0, 0, size), [this]() {
        _context.withDepthTestMode(DepthTestMode::None, [this]() {
            _context.clearColor();
            _meshRegistry.get(MeshName::quadNDC).draw(_statistic);
        });
    });
}

void RenderPipelineBase::applyBloom(Texture &scene, Texture &hilights, Texture &revealage, Framebuffer &dst) {
    if (_bloom.down.empty()) {
        return;
    }
    int levels = static_cast<int>(_bloom.down.size());
    auto setTexel = [this](glm::ivec2 sourceSize, float bloomLevels) {
        _uniforms.setScreenEffect([&](auto &se) {
            se.screenResolution = glm::vec2(sourceSize);
            se.screenResolutionRcp = 1.0f / se.screenResolution;
            se.bloomParams = glm::vec4(kBloomThreshold, kBloomKnee, _options.bloomIntensity, kBloomGlowWeight);
            se.bloomLevels = bloomLevels;
        });
    };
    setTexel(_targetSize, 0.0f);
    _context.bindTexture(scene);
    _context.bindTexture(hilights, TextureUnits::hilights);
    _context.bindTexture(revealage, TextureUnits::oitRevealage);
    drawFullscreen(ShaderProgramId::postBloomExtract, *_bloom.down[0].framebuffer, _bloom.down[0].size);
    for (int i = 1; i < levels; ++i) {
        setTexel(_bloom.down[i - 1].size, 0.0f);
        _context.bindTexture(*_bloom.down[i - 1].texture);
        drawFullscreen(ShaderProgramId::postBloomDown, *_bloom.down[i].framebuffer, _bloom.down[i].size);
    }
    for (int i = levels - 2; i >= 0; --i) {
        auto &smaller = (i == levels - 2) ? _bloom.down[i + 1] : _bloom.up[i + 1];
        setTexel(smaller.size, 0.0f);
        _context.bindTexture(*smaller.texture);
        _context.bindTexture(*_bloom.down[i].texture, TextureUnits::hilights);
        drawFullscreen(ShaderProgramId::postBloomUp, *_bloom.up[i].framebuffer, _bloom.up[i].size);
    }
    setTexel(_targetSize, static_cast<float>(levels));
    _context.bindTexture(scene);
    _context.bindTexture(levels >= 2 ? *_bloom.up[0].texture : *_bloom.down[0].texture, TextureUnits::hilights);
    drawFullscreen(ShaderProgramId::postBloomCombine, dst, _targetSize);
}

std::unique_ptr<IRenderPipeline> RenderPipelineFactory::create(RendererType type, glm::ivec2 targetSize) {
    switch (type) {
    case RendererType::Retro:
        return std::make_unique<RetroRenderPipeline>(
            std::move(targetSize),
            _options,
            _context,
            _meshRegistry,
            _shaderRegistry,
            _statistic,
            _textureRegistry,
            _uniforms);
    case RendererType::PBR:
        return std::make_unique<PBRRenderPipeline>(
            std::move(targetSize),
            _options,
            _context,
            _meshRegistry,
            _pbrTextures,
            _shaderRegistry,
            _statistic,
            _textureRegistry,
            _uniforms);
    default:
        throw std::invalid_argument("Unsupported renderer type: " + std::to_string(static_cast<int>(type)));
    }
}

} // namespace scene

} // namespace reone
