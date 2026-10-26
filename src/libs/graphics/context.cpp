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

#include "reone/graphics/context.h"

#include "SDL3/SDL_video.h"

#include "reone/graphics/framebuffer.h"
#include "reone/graphics/shaderprogram.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/uniformbuffer.h"
#include "reone/system/logutil.h"
#include "reone/system/renderdiag.h"
#include "reone/system/threadutil.h"

namespace reone {

namespace graphics {

static constexpr GLenum kColorAttachments[] {
    GL_COLOR_ATTACHMENT0,
    GL_COLOR_ATTACHMENT1,
    GL_COLOR_ATTACHMENT2,
    GL_COLOR_ATTACHMENT3,
    GL_COLOR_ATTACHMENT4,
    GL_COLOR_ATTACHMENT5,
    GL_COLOR_ATTACHMENT6,
    GL_COLOR_ATTACHMENT7};

// KHR_debug is loaded by hand: the bundled GL loader stops at 4.0 core.
namespace {

constexpr GLenum kGlDebugOutput = 0x92E0;
constexpr GLenum kGlDebugOutputSynchronous = 0x8242;
constexpr GLenum kGlDebugTypeError = 0x824C;
constexpr GLenum kGlDebugTypeUndefinedBehavior = 0x824E;
constexpr GLenum kGlDebugTypeDeprecated = 0x824D;
constexpr GLenum kGlDebugTypePortability = 0x824F;
constexpr GLenum kGlDebugTypePerformance = 0x8250;
constexpr GLenum kGlDebugSeverityHigh = 0x9146;
constexpr GLenum kGlDebugSeverityMedium = 0x9147;

constexpr GLenum kGlDebugSourceApplication = 0x824A;

using PushDebugGroupProc = void(GLAD_API_PTR *)(GLenum, GLuint, GLsizei, const GLchar *);
using PopDebugGroupProc = void(GLAD_API_PTR *)();
PushDebugGroupProc glPushDebugGroupPtr = nullptr;
PopDebugGroupProc glPopDebugGroupPtr = nullptr;

using DebugProc = void(GLAD_API_PTR *)(GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar *, const void *);
using DebugMessageCallbackProc = void(GLAD_API_PTR *)(DebugProc, const void *);

void GLAD_API_PTR onGlDebugMessage(GLenum source, GLenum type, GLuint id, GLenum severity,
                               GLsizei length, const GLchar *message, const void *) {
    // Errors and undefined behaviour always; other types only when severe.
    bool error = type == kGlDebugTypeError || type == kGlDebugTypeUndefinedBehavior;
    bool severe = severity == kGlDebugSeverityHigh || severity == kGlDebugSeverityMedium;
    if (!error && !(severe && (type == kGlDebugTypeDeprecated || type == kGlDebugTypePortability || type == kGlDebugTypePerformance))) {
        return;
    }
    std::string text = message ? std::string(message, length > 0 ? static_cast<size_t>(length) : std::strlen(message)) : std::string();
    renderdiag::report("gl", "debug:" + std::to_string(id),
                       "GL debug output (type 0x" + str(boost::format("%x") % type) + ", id " + std::to_string(id) + "): " + text);
}

} // namespace

void Context::installDebugOutput() {
    if (!SDL_GL_ExtensionSupported("GL_KHR_debug")) {
        info("GL_KHR_debug not available: render diagnostics use glGetError only", LogChannel::Render);
        return;
    }
    glPushDebugGroupPtr = reinterpret_cast<PushDebugGroupProc>(SDL_GL_GetProcAddress("glPushDebugGroup"));
    glPopDebugGroupPtr = reinterpret_cast<PopDebugGroupProc>(SDL_GL_GetProcAddress("glPopDebugGroup"));
    if (!glPushDebugGroupPtr || !glPopDebugGroupPtr) {
        glPushDebugGroupPtr = nullptr;
        glPopDebugGroupPtr = nullptr;
    }
    auto callback = reinterpret_cast<DebugMessageCallbackProc>(SDL_GL_GetProcAddress("glDebugMessageCallback"));
    if (!callback) {
        return;
    }
    glEnable(kGlDebugOutput);
    glEnable(kGlDebugOutputSynchronous);
    callback(&onGlDebugMessage, nullptr);
}

void Context::checkErrors(const char *where) {
    for (int i = 0; i < 8; ++i) {
        GLenum code = glGetError();
        if (code == GL_NO_ERROR) {
            return;
        }
        auto hex = str(boost::format("0x%04x") % code);
        renderdiag::report("gl", std::string(where) + ":" + hex,
                           "glGetError " + hex + " after " + where);
    }
}

void Context::init() {
    if (_inited) {
        return;
    }
    checkMainThread();
    if (!gladLoadGL(SDL_GL_GetProcAddress)) {
        // Attempt to retrieve basic version information manually for diagnosis
        // Use manual glGetString which might work even if GLAD failed to load everything
        const GLubyte *version = glGetString(GL_VERSION);
        const GLubyte *renderer = glGetString(GL_RENDERER);
        const GLubyte *vendor = glGetString(GL_VENDOR);

        std::string versionStr = version ? (const char *)version : "Unknown";
        std::string rendererStr = renderer ? (const char *)renderer : "Unknown";
        std::string vendorStr = vendor ? (const char *)vendor : "Unknown";

        throw std::runtime_error(str(boost::format(
                                         "gladLoadGLLoader failed to initialize OpenGL context.\n"
                                         "  GL_VERSION: %s\n"
                                         "  GL_RENDERER: %s\n"
                                         "  GL_VENDOR: %s\n"
                                         "  Ensure your graphics driver supports OpenGL 4.0 Core Profile.") %
                                     versionStr % rendererStr % vendorStr));
    }

    int maxBuffers;
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxBuffers);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    glm::ivec4 viewport(0.0f);
    glGetIntegerv(GL_VIEWPORT, &viewport[0]);
    _viewports.push(std::move(viewport));

    setDepthTestMode(DepthTestMode::LessOrEqual);
    _depthTestModes.push(DepthTestMode::LessOrEqual);

    setDepthMask(true);
    _depthMasks.push(true);

    setPolygonMode(PolygonMode::Fill);
    _polygonModes.push(PolygonMode::Fill);

    setFaceCullMode(FaceCullMode::None);
    _faceCullModes.push(FaceCullMode::None);

    setBlendMode(BlendMode::None);
    _blendModes.push(BlendMode::None);

    installDebugOutput();

    _inited = true;
}

void Context::clearColor(glm::vec4 color) {
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Context::clearDepth() {
    glClear(GL_DEPTH_BUFFER_BIT);
}

void Context::clearColorDepth(glm::vec4 color) {
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Context::resetProgram() {
    if (!_program) {
        return;
    }
    glUseProgram(0);
    _program.reset();
}

void Context::useProgram(ShaderProgram &program) {
    if (_program && &_program->get() == &program) {
        return;
    }
    program.use();
    _program = program;
}

void Context::resetDrawFramebuffer() {
    if (_drawFramebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        _drawFramebuffer.reset();
    }
}

void Context::resetReadFramebuffer() {
    if (_readFramebuffer) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        _readFramebuffer.reset();
    }
}

void Context::bindDrawFramebuffer(Framebuffer &buffer, std::vector<int> colorIndices) {
    if (!_drawFramebuffer || _drawFramebuffer->get().nameGL() != buffer.nameGL()) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, buffer.nameGL());
        _drawFramebuffer = buffer;
    }
    if (!colorIndices.empty()) {
        auto attachments = std::vector<GLenum>(colorIndices.size());
        for (size_t i = 0; i < colorIndices.size(); ++i) {
            attachments[i] = kColorAttachments[colorIndices[i]];
        }
        glDrawBuffers(attachments.size(), &attachments[0]);
    } else {
        glDrawBuffer(GL_NONE);
    }
}

void Context::bindReadFramebuffer(Framebuffer &buffer, std::optional<int> colorIdx) {
    if (!_readFramebuffer || _readFramebuffer->get().nameGL() != buffer.nameGL()) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, buffer.nameGL());
        _readFramebuffer = buffer;
    }
    if (colorIdx) {
        glReadBuffer(kColorAttachments[*colorIdx]);
    } else {
        glReadBuffer(GL_NONE);
    }
}

void Context::blitFramebuffer(Framebuffer &source,
                              Framebuffer &destination,
                              const glm::ivec4 &srcRect,
                              const glm::ivec4 &dstRect,
                              int srcColorIdx,
                              int dstColorIdx,
                              int mask,
                              FramebufferBlitFilter filter) {
    bindReadFramebuffer(source, srcColorIdx);
    bindDrawFramebuffer(destination, {dstColorIdx});
    GLenum glMask = 0;
    if ((mask & FramebufferBlitFlags::color) != 0) {
        glMask |= GL_COLOR_BUFFER_BIT;
    }
    if ((mask & FramebufferBlitFlags::depth) != 0) {
        glMask |= GL_DEPTH_BUFFER_BIT;
    }
    if ((mask & FramebufferBlitFlags::stencil) != 0) {
        glMask |= GL_STENCIL_BUFFER_BIT;
    }
    GLenum glFilter = 0;
    switch (filter) {
    case FramebufferBlitFilter::Nearest:
        glFilter = GL_NEAREST;
        break;
    case FramebufferBlitFilter::Linear:
        glFilter = GL_LINEAR;
        break;
    default:
        throw std::invalid_argument("filter");
    }
    // GL only allows GL_NEAREST for depth and stencil: copy colour with the
    // requested filter and depth/stencil nearest, in two blits when both are asked for.
    GLenum depthStencil = glMask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    if (glFilter == GL_LINEAR && depthStencil != 0) {
        if ((glMask & GL_COLOR_BUFFER_BIT) != 0) {
            glBlitFramebuffer(srcRect[0], srcRect[1], srcRect[2], srcRect[3],
                              dstRect[0], dstRect[1], dstRect[2], dstRect[3],
                              GL_COLOR_BUFFER_BIT,
                              GL_LINEAR);
        }
        glMask = depthStencil;
        glFilter = GL_NEAREST;
    }
    glBlitFramebuffer(srcRect[0], srcRect[1], srcRect[2], srcRect[3],
                      dstRect[0], dstRect[1], dstRect[2], dstRect[3],
                      glMask,
                      glFilter);
}

std::shared_ptr<Texture> Context::captureScreen(int width, int height) {
    auto pixels = std::make_shared<ByteBuffer>();
    pixels->resize(static_cast<size_t>(3) * width * height);
    // GUI scene controls render through child framebuffers and may leave one
    // bound for reading. A screenshot is the composed window back buffer.
    _readFramebuffer.reset();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels->data());

    auto texture = std::make_shared<Texture>("screenshot", TextureType::TwoDim, Texture::Properties {});
    texture->setPixels(width, height, PixelFormat::RGB8, Texture::Layer {pixels});
    return texture;
}

std::vector<float> Context::captureScreenDepth(int width, int height) {
    std::vector<float> depth(static_cast<size_t>(width) * height, 1.0f);
    _readFramebuffer.reset();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    while (glGetError() != GL_NO_ERROR) {
    }
    glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
    if (glGetError() != GL_NO_ERROR) {
        return {};
    }
    return depth;
}

void Context::pushDebugGroup(const std::string &name) {
    if (glPushDebugGroupPtr) {
        glPushDebugGroupPtr(kGlDebugSourceApplication, 0, static_cast<GLsizei>(name.size()), name.c_str());
    }
}

void Context::popDebugGroup() {
    if (glPopDebugGroupPtr) {
        glPopDebugGroupPtr();
    }
}

void Context::withColorWrites(bool enabled, const std::function<void()> &block) {
    GLboolean previous[4];
    glGetBooleanv(GL_COLOR_WRITEMASK, previous);
    glColorMask(enabled, enabled, enabled, enabled);
    block();
    glColorMask(previous[0], previous[1], previous[2], previous[3]);
}

void Context::bindUniformBuffer(UniformBuffer &buffer, int index) {
    buffer.bind(index);
}

void Context::bindTexture(Texture &texture, int unit) {
    if (_activeTexUnit != unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        _activeTexUnit = unit;
    }
    texture.bind();
}

void Context::pushViewport(glm::ivec4 viewport) {
    setViewport(viewport);
    _viewports.push(std::move(viewport));
}

void Context::pushDepthTestMode(DepthTestMode mode) {
    setDepthTestMode(mode);
    _depthTestModes.push(mode);
}

void Context::pushDepthMask(bool enabled) {
    setDepthMask(enabled);
    _depthMasks.push(enabled);
}

void Context::pushPolygonMode(PolygonMode mode) {
    setPolygonMode(mode);
    _polygonModes.push(mode);
}

void Context::pushFaceCullMode(FaceCullMode mode) {
    setFaceCullMode(mode);
    _faceCullModes.push(mode);
}

void Context::pushBlendMode(BlendMode mode) {
    setBlendMode(mode);
    _blendModes.push(mode);
}

void Context::popViewport() {
    _viewports.pop();
    setViewport(_viewports.top());
}

void Context::popDepthTestMode() {
    _depthTestModes.pop();
    setDepthTestMode(_depthTestModes.top());
}

void Context::popDepthMask() {
    _depthMasks.pop();
    setDepthMask(_depthMasks.top());
}

void Context::popPolygonMode() {
    _polygonModes.pop();
    setPolygonMode(_polygonModes.top());
}

void Context::popFaceCullMode() {
    _faceCullModes.pop();
    setFaceCullMode(_faceCullModes.top());
}

void Context::popBlendMode() {
    _blendModes.pop();
    setBlendMode(_blendModes.top());
}

void Context::withViewport(glm::ivec4 viewport, const std::function<void()> &block) {
    if (_viewports.top() == viewport) {
        block();
        return;
    }
    pushViewport(std::move(viewport));
    block();
    popViewport();
}

void Context::withScissorTest(const glm::ivec4 &bounds, const std::function<void()> &block) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(bounds[0], bounds[1], bounds[2], bounds[3]);
    glClear(GL_COLOR_BUFFER_BIT);

    block();

    glDisable(GL_SCISSOR_TEST);
}

void Context::withScissorTestNoClear(const glm::ivec4 &bounds, const std::function<void()> &block) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(bounds[0], bounds[1], bounds[2], bounds[3]);
    block();
    glDisable(GL_SCISSOR_TEST);
}

void Context::withDepthTestMode(DepthTestMode mode, const std::function<void()> &block) {
    if (_depthTestModes.top() == mode) {
        block();
        return;
    }
    setDepthTestMode(mode);
    _depthTestModes.push(mode);

    block();

    _depthTestModes.pop();
    setDepthTestMode(_depthTestModes.top());
}

void Context::withDepthMask(bool enabled, const std::function<void()> &block) {
    if (_depthMasks.top() == enabled) {
        block();
        return;
    }
    setDepthMask(enabled);
    _depthMasks.push(enabled);

    block();

    _depthMasks.pop();
    setDepthMask(_depthMasks.top());
}

void Context::withPolygonMode(PolygonMode mode, const std::function<void()> &block) {
    if (_polygonModes.top() == mode) {
        block();
        return;
    }
    pushPolygonMode(mode);
    block();
    popPolygonMode();
}

void Context::withFaceCullMode(FaceCullMode mode, const std::function<void()> &block) {
    if (_faceCullModes.top() == mode) {
        block();
        return;
    }
    pushFaceCullMode(mode);
    block();
    popFaceCullMode();
}

void Context::withBlendMode(BlendMode mode, const std::function<void()> &block) {
    if (_blendModes.top() == mode) {
        block();
        return;
    }
    pushBlendMode(mode);
    block();
    popBlendMode();
}

void Context::setViewport(glm::ivec4 viewport) {
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void Context::setDepthTestMode(DepthTestMode mode) {
    if (mode == DepthTestMode::None) {
        glDisable(GL_DEPTH_TEST);
    } else {
        glEnable(GL_DEPTH_TEST);
        switch (mode) {
            break;
        case DepthTestMode::Equal:
            glDepthFunc(GL_EQUAL);
            break;
        case DepthTestMode::LessOrEqual:
            glDepthFunc(GL_LEQUAL);
            break;
        case DepthTestMode::Always:
            glDepthFunc(GL_ALWAYS);
            break;
        case DepthTestMode::Less:
        default:
            glDepthFunc(GL_LESS);
            break;
        }
    }
}

void Context::setDepthMask(bool enabled) {
    glDepthMask(enabled ? GL_TRUE : GL_FALSE);
}

void Context::setPolygonMode(PolygonMode mode) {
    if (mode == PolygonMode::Line) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    } else {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
}

void Context::setFaceCullMode(FaceCullMode mode) {
    if (mode == FaceCullMode::None) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        if (mode == FaceCullMode::Front) {
            glCullFace(GL_FRONT);
        } else {
            glCullFace(GL_BACK);
        }
    }
}

void Context::setBlendMode(BlendMode mode) {
    if (mode == BlendMode::None) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        switch (mode) {
        case BlendMode::Additive:
            glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
            break;
        case BlendMode::Lighten:
            glBlendEquationSeparate(GL_MAX, GL_FUNC_ADD);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
            break;
        case BlendMode::OIT_Transparent:
            glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::PremultipliedAlpha:
            glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::Normal:
        default:
            glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
            break;
        }
    }
}

} // namespace graphics

} // namespace reone
