#pragma once

#include "common/gl_raii.hpp"
#include "game/render/GpuBuffer.hpp"
#include "game/render/MegaScene.hpp"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>

namespace game::render
{

/// Per-frame CPU inputs passed into Renderer::renderFrame().
struct RenderContext
{
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::vec3 cameraPos{0.0f};
    float nearPlane = 0.1f;
    float elapsedTime = 0.0f;
    float deltaTime = 0.0f;
    std::uint32_t frameIndex = 0;
    int viewportWidth = 1280;
    int viewportHeight = 720;
};

/// std140 UBO bound at binding = 0 across culling, G-buffer, and lighting passes.
struct alignas(16) FrameUniforms
{
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::mat4 viewProj{1.0f};
    glm::mat4 invViewProj{1.0f};
    glm::vec4 cameraPos{0.0f};
    std::array<glm::vec4, 6> frustumPlanes{};
    glm::vec4 screenParams{0.0f}; // x=w, y=h, z=1/w, w=1/h
    glm::vec4 timeParams{0.0f};   // x=time, y=dt, z=near, w=0
    glm::uvec4 counts{0u};        // x=objectCount, y=meshCount, z=lightCount, w=frameIndex
};

/// Shared GPU render targets, FBO bundles, scene buffers, and material array owned by Renderer.
class RenderResources
{
public:
    RenderResources();
    ~RenderResources();

    RenderResources(const RenderResources &) = delete;
    RenderResources &operator=(const RenderResources &) = delete;

    void onResize(int width, int height);
    void updateFrameUniforms(const RenderContext &ctx, std::uint32_t lightCount = 0);
    void drawFullscreenTriangle() const noexcept;

    [[nodiscard]] MegaScene &scene() noexcept { return m_scene; }
    [[nodiscard]] const MegaScene &scene() const noexcept { return m_scene; }

    [[nodiscard]] const GpuBuffer &frameUbo() const noexcept { return m_frameUbo; }
    [[nodiscard]] const gl::Texture &gbuffer0() const noexcept { return m_gbuffer0; }
    [[nodiscard]] const gl::Texture &gbuffer1() const noexcept { return m_gbuffer1; }
    [[nodiscard]] const gl::Texture &depthTex() const noexcept { return m_depthTex; }
    [[nodiscard]] const gl::Texture &hdrTex() const noexcept { return m_hdrTex; }

    [[nodiscard]] const gl::Framebuffer &gbufferFbo() const noexcept { return m_gbufferFbo; }
    [[nodiscard]] const gl::Framebuffer &puddleFbo() const noexcept { return m_puddleFbo; }
    [[nodiscard]] const gl::Framebuffer &hdrFbo() const noexcept { return m_hdrFbo; }

    [[nodiscard]] GLuint materialArrayTexId() const noexcept { return m_materialArrayTex; }
    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }

private:
    void createStarterMaterialArray();

    int m_width = 0;
    int m_height = 0;

    MegaScene m_scene;
    GpuBuffer m_frameUbo;

    gl::Texture m_gbuffer0; // RGBA8: Albedo.rgb + AO
    gl::Texture m_gbuffer1; // RGBA16F: WorldNormal.xyz + Roughness
    gl::Texture m_depthTex; // DEPTH_COMPONENT32F (Reversed-Z [0, 1])
    gl::Texture m_hdrTex;   // RGBA16F: Lit HDR accumulation

    gl::Framebuffer m_gbufferFbo;
    gl::Framebuffer m_puddleFbo; // Color attachments only (no depth attached so depthTex can be sampled safely)
    gl::Framebuffer m_hdrFbo;

    GLuint m_materialArrayTex = 0;
    GLuint m_emptyVao = 0;
};

} // namespace game::render
