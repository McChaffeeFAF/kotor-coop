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

#include "reone/graphics/framebuffer.h"
#include "reone/graphics/renderbuffer.h"
#include "reone/graphics/texture.h"

#include "pass.h"

template <>
struct std::hash<glm::ivec2> {
    size_t operator()(const glm::ivec2 &dim) const {
        size_t hash = 0;
        boost::hash_combine(hash, dim.x);
        boost::hash_combine(hash, dim.y);
        return hash;
    }
};

namespace reone {

namespace graphics {

class IStatistic;

class Context;
class MeshRegistry;
class PBRTextures;
class ShaderRegistry;
class TextureRegistry;
class Uniforms;

struct GraphicsOptions;

} // namespace graphics

namespace scene {

enum class RendererType {
    Retro,
    PBR
};

class IRenderPipeline {
public:
    virtual ~IRenderPipeline() = default;

    virtual void init() = 0;

    virtual void reset() = 0;
    virtual void inRenderPass(RenderPassName name, std::function<void(IRenderPass &)> block) = 0;

    virtual graphics::Texture &render() = 0;

    /**
     * This frame's camera, before render(). Temporal effects keep it for
     * reprojection; it never feeds back into the camera or the simulation.
     */
    virtual void setCamera(const glm::mat4 &projection, const glm::mat4 &view) {}

    /** Sub-pixel offset (in pixels) to add to this frame's projection; zero without TAA. */
    virtual glm::vec2 projectionJitter() const { return glm::vec2(0.0f); }

    /** A frame number for screen-space noise that TAA averages; 0 without TAA. */
    virtual float temporalNoiseFrame() const { return 0.0f; }

    /**
     * The opaque scene depth of the last render() as a texture, when the
     * options keep one (sceneDepthTextureNeeded); nullptr otherwise.
     */
    virtual graphics::Texture *sceneDepth() { return nullptr; }

    /** Seconds since the last frame, for eye adaptation; it never reaches the simulation. */
    virtual void setFrameTime(float dt) {}

    /** Reflection probe faces (reflections on): the last render() becomes face `face` (0-5). */
    virtual void storeProbeFace(int face) {}
    virtual void setProbeValid(bool valid) {}
    /** Forget temporal history (TAA, eye adaptation) after off-screen renders. */
    virtual void resetTemporal() {}
};

/** Reflection probe faces: direction and up of face i (world, Z up), shared with f_pp_ssr. */
glm::vec3 probeFaceDirection(int face);
glm::vec3 probeFaceUp(int face);

/** True when the options draw the scene into half-float targets (tone mapping, auto exposure). */
bool hdrSceneTargets(const graphics::GraphicsOptions &options);

/** True when the options need the opaque depth as a texture (TAA, GTAO, ReShade mode). */
bool sceneDepthTextureNeeded(const graphics::GraphicsOptions &options);

/**
 * The options a pipeline's render targets depend on, as a string: a scene
 * graph makes a new pipeline when it changes. Options read per frame
 * (sharpen, soft shadows, intensities) are not part of it.
 */
std::string renderPipelineKey(const graphics::GraphicsOptions &options);

class IRenderPipelineFactory {
public:
    virtual ~IRenderPipelineFactory() = default;

    virtual std::unique_ptr<IRenderPipeline> create(RendererType type, glm::ivec2 targetSize) = 0;
};

class RenderPipelineBase : public IRenderPipeline, boost::noncopyable {
public:
    using RenderPassCallback = std::function<void(IRenderPass &)>;

    void reset() override {
        _passCallbacks.clear();
    }

    void setCamera(const glm::mat4 &projection, const glm::mat4 &view) override;
    glm::vec2 projectionJitter() const override { return _taa.jitter; }
    float temporalNoiseFrame() const override {
        return _taa.history[0].texture ? static_cast<float>(_taa.frame % 64) : 0.0f;
    }

    void setFrameTime(float dt) override { _frameTime = dt; }
    void setProbeValid(bool valid) override { _probeValid = valid && _probe.texture; }
    void resetTemporal() override {
        _taa.valid = false;
        _motion.valid = false;
        _tone.valid = false;
    }

    void inRenderPass(RenderPassName name, RenderPassCallback callback) override {
        _passCallbacks[name] = std::move(callback);
    }

protected:
    struct GaussianBlurParams {
        bool vertical {false};
        bool strong {false};
    };

    glm::ivec2 _targetSize;
    graphics::GraphicsOptions &_options;
    graphics::Context &_context;
    graphics::MeshRegistry &_meshRegistry;
    graphics::ShaderRegistry &_shaderRegistry;
    graphics::IStatistic &_statistic;
    graphics::TextureRegistry &_textureRegistry;
    graphics::Uniforms &_uniforms;

    bool _inited {false};

    glm::mat4 _shadowLightSpace[graphics::kNumShadowLightSpace] {glm::mat4(1.0f)};
    glm::vec4 _shadowCascadeFarPlanes {glm::vec4(0.0f)};

    RenderPassName _passName {RenderPassName::None};
    std::map<RenderPassName, RenderPassCallback> _passCallbacks;

    RenderPipelineBase(glm::ivec2 targetSize,
                       graphics::GraphicsOptions &options,
                       graphics::Context &context,
                       graphics::MeshRegistry &meshRegistry,
                       graphics::ShaderRegistry &shaderRegistry,
                       graphics::IStatistic &statistic,
                       graphics::TextureRegistry &textureRegistry,
                       graphics::Uniforms &uniforms) :
        _targetSize(std::move(targetSize)),
        _options(options),
        _context(context),
        _meshRegistry(meshRegistry),
        _shaderRegistry(shaderRegistry),
        _statistic(statistic),
        _textureRegistry(textureRegistry),
        _uniforms(uniforms) {
    }

    void applyBoxBlur(graphics::Texture &tex, graphics::Framebuffer &dst, const glm::ivec2 &size);
    void applyGaussianBlur(graphics::Texture &tex, graphics::Framebuffer &dst, const glm::ivec2 &size, const GaussianBlurParams &params);
    void applyMedianFilter(graphics::Texture &tex, graphics::Framebuffer &dst, const glm::ivec2 &size, bool strong = false);
    void applyFXAA(graphics::Texture &tex, graphics::Framebuffer &dst, const glm::ivec2 &size);
    void applySharpen(graphics::Texture &tex, graphics::Framebuffer &dst, const glm::ivec2 &size, float amount);

    // Enhanced preset post-processing, shared by both pipelines. Each init
    // makes targets only for the features on (renderPipelineKey).

    struct ColorTarget {
        std::shared_ptr<graphics::Texture> texture;
        std::shared_ptr<graphics::Framebuffer> framebuffer;
        glm::ivec2 size {0};
    };

    struct BloomTargets {
        std::vector<ColorTarget> down; /**< half resolution, then halving */
        std::vector<ColorTarget> up;   /**< up[i] = down[i] + upsampled up[i + 1] */
    };

    BloomTargets _bloom;

    struct SMAATargets {
        ColorTarget edges;   /**< RG8: left and top edges */
        ColorTarget weights; /**< RGBA8: blending weights */
    };

    SMAATargets _smaa;

    struct TAAState {
        ColorTarget history[2]; /**< RGBA16F, ping-ponged */
        int current {0};
        bool valid {false};
        uint32_t frame {0};
        glm::vec2 jitter {0.0f};
        glm::mat4 viewProjection {1.0f};
        glm::mat4 previousViewProjection {1.0f};
        glm::vec3 cameraPosition {0.0f};
        glm::vec3 previousCameraPosition {0.0f};
        glm::vec3 cameraForward {0.0f, 0.0f, -1.0f};
        glm::vec3 previousCameraForward {0.0f, 0.0f, -1.0f};
    };

    TAAState _taa;

    struct AOTargets {
        ColorTarget raw;  /**< RG16F: AO, linear depth; also the blurred result */
        ColorTarget blur; /**< RG16F: the horizontal blur */
        ColorTarget white; /**< 1x1 white, for an AO-enabled shader with AO off */
    };

    AOTargets _ao;

    struct ToneTargets {
        ColorTarget meter;      /**< RG16F: per tile, weighted log2 luma and weight */
        ColorTarget adapted[2]; /**< 1x1 RGBA16F: the eased EV, ping-ponged */
        int current {0};
        bool valid {false};
    };

    ToneTargets _tone;

    ColorTarget _volume; /**< half resolution RGBA16F: in-scattered light, transmittance */
    ColorTarget _gi;     /**< half resolution RGBA16F: gathered bounce light */
    ColorTarget _probe;  /**< reflection probe: six square faces side by side */
    std::shared_ptr<graphics::Texture> _gradeLUT;
    std::string _gradeLUTProfile;

    /** The grading profile in effect: the override, else the module's; empty is neutral. */
    std::string activeGradingProfile() const;
    struct MotionBlurState {
        glm::mat4 viewProjection {1.0f};
        glm::mat4 previousViewProjection {1.0f};
        glm::vec3 cameraPosition {0.0f};
        glm::vec3 previousCameraPosition {0.0f};
        bool valid {false};
    };
    MotionBlurState _motion;

    /** Camera motion blur of `src` into `dst` from `depth`; the history moves on every call. */
    void applyMotionBlur(graphics::Texture &src, graphics::Texture &depth, graphics::Framebuffer &dst);

    uint32_t _lensFrame {0};
    /** True when any lens effect is on. */
    bool lensEffectsOn() const;
    /** The lens effects of `src` into `dst`. */
    void applyLensEffects(graphics::Texture &src, graphics::Framebuffer &dst);

    /** Conversation depth of field of `src` into `dst` from `depth`. */
    void applyDOF(graphics::Texture &src, graphics::Texture &depth, graphics::Framebuffer &dst);

    /** Colour grading of `src` into `dst`; false (nothing drawn) when neutral. */
    bool applyGrading(graphics::Texture &src, graphics::Framebuffer &dst);
    bool _probeValid {false};
    uint32_t _ssrFrame {0};
    glm::mat4 _view {1.0f};

    /** SSR (and the probe) over `opaque`, whose alpha is the reflectivity, into `dst`. */
    void applySSR(graphics::Texture &opaque, graphics::Texture &depth, graphics::Framebuffer &dst);
    uint32_t _giFrame {0};

    /** One-bounce SSGI over `opaque` (colour) from `depth`, into `dst`. */
    void applySSGI(graphics::Texture &opaque, graphics::Texture &depth, graphics::Framebuffer &dst);
    uint32_t _volumeFrame {0};

    /**
     * Volumetric light over `scene` (the composed frame) into `dst`, from
     * the opaque `depth`; `shadowType` is 0 (none), 1 (sun) or 2 (point).
     */
    void applyVolumetrics(graphics::Texture &scene, graphics::Texture &depth, int shadowType, graphics::Framebuffer &dst);
    float _frameTime {0.0f};

    /**
     * Eye adaptation and tone mapping of `src` (the HDR frame) into `dst`;
     * either may be off (auto exposure alone clamps to 1.0, tone mapping
     * alone uses no exposure change).
     */
    void applyToneMapping(graphics::Texture &src, graphics::Framebuffer &dst);

    static ColorTarget makeColorTarget(const std::string &name, glm::ivec2 size, graphics::PixelFormat format);

    void initEnhancedTargets();

    /**
     * Bloom: the bright part of `scene` plus the glow channel (`hilights`,
     * the blurred hilights buffer, and the particle glow in `revealage`),
     * through a downsample/upsample chain; `dst` gets scene + bloom.
     */
    void applyBloom(graphics::Texture &scene, graphics::Texture &hilights, graphics::Texture &revealage, graphics::Framebuffer &dst);

    /**
     * TAA resolve of `src` against the history, reprojected with `depth`
     * (the opaque depth); the result is the new history, also blitted to `dst`.
     */
    void applyTAA(graphics::Texture &src, graphics::Texture &depth, graphics::Framebuffer &dst);

    /** GTAO from `depth`, blurred; returns the AO texture (r). */
    graphics::Texture &applyGTAO(graphics::Texture &depth);

    /** SMAA 1x-style morphological AA of `src` into `dst` (full resolution). */
    void applySMAA(graphics::Texture &src, graphics::Framebuffer &dst);

    /**
     * The anti-aliasing and sharpening at the end of the frame, in place on
     * `output` (via `ping`/`pong`): false for the retail FXAA/sharpen chain;
     * true after SMAA or TAA (which needs `depth`) and sharpening.
     */
    bool applyEnhancedAntiAliasing(graphics::Texture *depth,
                                   graphics::Texture &outputColor,
                                   graphics::Framebuffer &output,
                                   graphics::Texture &pingColor,
                                   graphics::Framebuffer &ping,
                                   graphics::Texture &pongColor,
                                   graphics::Framebuffer &pong);

    void drawFullscreen(const std::string &program, graphics::Framebuffer &dst, const glm::ivec2 &size);
};

class RenderPipelineFactory : public IRenderPipelineFactory, boost::noncopyable {
public:
    RenderPipelineFactory(graphics::GraphicsOptions &options,
                          graphics::Context &context,
                          graphics::MeshRegistry &meshRegistry,
                          graphics::PBRTextures &pbrTextures,
                          graphics::ShaderRegistry &shaderRegistry,
                          graphics::IStatistic &statistic,
                          graphics::TextureRegistry &textureRegistry,
                          graphics::Uniforms &uniforms) :
        _options(options),
        _context(context),
        _meshRegistry(meshRegistry),
        _pbrTextures(pbrTextures),
        _shaderRegistry(shaderRegistry),
        _statistic(statistic),
        _textureRegistry(textureRegistry),
        _uniforms(uniforms) {
    }

    std::unique_ptr<IRenderPipeline> create(RendererType type, glm::ivec2 targetSize) override;

private:
    graphics::GraphicsOptions &_options;
    graphics::Context &_context;
    graphics::MeshRegistry &_meshRegistry;
    graphics::PBRTextures &_pbrTextures;
    graphics::ShaderRegistry &_shaderRegistry;
    graphics::IStatistic &_statistic;
    graphics::TextureRegistry &_textureRegistry;
    graphics::Uniforms &_uniforms;
};

} // namespace scene

} // namespace reone
