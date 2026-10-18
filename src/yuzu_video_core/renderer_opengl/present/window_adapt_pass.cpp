// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "yuzu_common/settings.h"
#include "yuzu_video_core/fsr.h"
#include "video_settings.h"
#include "yuzu_video_core/framebuffer_config.h"
#include "yuzu_video_core/host_shaders/opengl_present_vert.h"
#include "yuzu_video_core/host_shaders/opengl_present_frag.h"
#include "yuzu_video_core/renderer_opengl/gl_device.h"
#include "yuzu_video_core/renderer_opengl/gl_shader_manager.h"
#include "yuzu_video_core/renderer_opengl/gl_shader_util.h"
#include "yuzu_video_core/renderer_opengl/present/layer.h"
#include "yuzu_video_core/renderer_opengl/present/present_uniforms.h"
#include "yuzu_video_core/renderer_opengl/present/window_adapt_pass.h"

namespace OpenGL {

WindowAdaptPass::WindowAdaptPass(const Device& device_, OGLSampler&& sampler_,
                                 std::string_view frag_source, bool use_rcas_, bool use_easu_, bool use_catmull_, bool use_cas_)
    : device(device_), use_rcas(use_rcas_), use_easu(use_easu_), use_catmull(use_catmull_), use_cas(use_cas_), sampler(std::move(sampler_)) {
    vert = CreateProgram(HostShaders::OPENGL_PRESENT_VERT, GL_VERTEX_SHADER);
    frag = CreateProgram(frag_source, GL_FRAGMENT_SHADER);

    // Generate VBO handle for drawing
    vertex_buffer.Create();

    // Attach vertex data to VAO
    glNamedBufferData(vertex_buffer.handle, sizeof(ScreenRectVertex) * 4, nullptr, GL_STREAM_DRAW);

    // Query vertex buffer address when the driver supports unified vertex attributes
    if (device.HasVertexBufferUnifiedMemory()) {
        glMakeNamedBufferResidentNV(vertex_buffer.handle, GL_READ_ONLY);
        glGetNamedBufferParameterui64vNV(vertex_buffer.handle, GL_BUFFER_GPU_ADDRESS_NV,
                                         &vertex_buffer_address);
    }
}

WindowAdaptPass::~WindowAdaptPass() = default;

void WindowAdaptPass::DrawToFramebuffer(ProgramManager& program_manager, std::list<Layer>& layers,
                                        std::span<const Tegra::FramebufferConfig> framebuffers,
                                        const Layout::FramebufferLayout& layout, bool invert_y) {
    GLint old_read_fb;
    GLint old_draw_fb;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_fb);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw_fb);

    const size_t layer_count = framebuffers.size();
    std::vector<GLuint> textures(layer_count);
    std::vector<std::array<u32, 16>> easu_constants(layer_count);
    std::vector<std::array<GLfloat, 3 * 2>> matrices(layer_count);
    std::vector<std::array<ScreenRectVertex, 4>> vertices(layer_count);

    auto layer_it = layers.begin();
    for (size_t i = 0; i < layer_count; i++) {
        textures[i] = layer_it->ConfigureDraw(matrices[i], vertices[i], program_manager,
                                              framebuffers[i], layout, invert_y, easu_constants[i]);
        layer_it++;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, old_read_fb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, old_draw_fb);

    program_manager.BindPresentPrograms(vert.handle, frag.handle);
    if (use_rcas) {
        std::array<u32, 4> constants{};
        ::FSR::FsrRcasCon(constants.data(),
                          static_cast<float>(videoSettings.fsr_sharpening_slider) / 100.0f);
        glProgramUniform4uiv(frag.handle, 0, 1, constants.data());
    }

    glDisable(GL_FRAMEBUFFER_SRGB);
    glViewportIndexedf(0, 0.0f, 0.0f, static_cast<GLfloat>(layout.width),
                       static_cast<GLfloat>(layout.height));

    glEnableVertexAttribArray(PositionLocation);
    glEnableVertexAttribArray(TexCoordLocation);
    glVertexAttribDivisor(PositionLocation, 0);
    glVertexAttribDivisor(TexCoordLocation, 0);
    glVertexAttribFormat(PositionLocation, 2, GL_FLOAT, GL_FALSE,
                         offsetof(ScreenRectVertex, position));
    glVertexAttribFormat(TexCoordLocation, 2, GL_FLOAT, GL_FALSE,
                         offsetof(ScreenRectVertex, tex_coord));
    glVertexAttribBinding(PositionLocation, 0);
    glVertexAttribBinding(TexCoordLocation, 0);
    if (device.HasVertexBufferUnifiedMemory()) {
        glBindVertexBuffer(0, 0, 0, sizeof(ScreenRectVertex));
        glBufferAddressRangeNV(GL_VERTEX_ATTRIB_ARRAY_ADDRESS_NV, 0, vertex_buffer_address,
                               sizeof(decltype(vertices)::value_type));
    } else {
        glBindVertexBuffer(0, vertex_buffer.handle, 0, sizeof(ScreenRectVertex));
    }

    glBindSampler(0, sampler.handle);

    // Update background color before drawing
    glClearColor(videoSettings.bg_red / 255.0f,
                 videoSettings.bg_green / 255.0f,
                 videoSettings.bg_blue / 255.0f, 1.0f);

    glClear(GL_COLOR_BUFFER_BIT);

    for (size_t i = 0; i < layer_count; i++) {
        switch (framebuffers[i].blending) {
        case Tegra::BlendMode::Opaque:
        default:
            glDisablei(GL_BLEND, 0);
            break;
        case Tegra::BlendMode::Premultiplied:
            glEnablei(GL_BLEND, 0);
            glBlendFuncSeparatei(0, GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
            break;
        case Tegra::BlendMode::Coverage:
            glEnablei(GL_BLEND, 0);
            glBlendFuncSeparatei(0, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
            break;
        }

        if (use_easu) {
            const auto& screen = layout.screen;
            glProgramUniform4uiv(frag.handle, 0, 4, easu_constants[i].data());
            glProgramUniform4f(frag.handle, 4, static_cast<float>(screen.left),
                               static_cast<float>(layout.height - screen.bottom),
                               static_cast<float>(layout.height - screen.top),
                               invert_y ? 1.0f : 0.0f);
            glProgramUniform1i(frag.handle, 5, easu_constants[i][0] != 0u ? 1 : 0);
        }
        glBindTextureUnit(0, textures[i]);
        if (use_catmull) glProgramUniform1ui(frag.handle, 1, easu_constants[i][0]);
        if (use_cas) glProgramUniform1f(frag.handle, 0, static_cast<float>(videoSettings.cas_sharpening_slider) / 100.0f);
        glProgramUniformMatrix3x2fv(vert.handle, ModelViewMatrixLocation, 1, GL_FALSE,
                                    matrices[i].data());
        glNamedBufferSubData(vertex_buffer.handle, 0, sizeof(vertices[i]), std::data(vertices[i]));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}

} // namespace OpenGL
