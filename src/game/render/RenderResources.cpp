#include "game/render/RenderResources.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace game::render
{

namespace
{

[[nodiscard]] std::array<glm::vec4, 6> extractReversedZFrustumPlanes(const glm::mat4 &vp) noexcept
{
    auto row = [&](int r) -> glm::vec4
    {
        return glm::vec4(vp[0][r], vp[1][r], vp[2][r], vp[3][r]);
    };

    const glm::vec4 r0 = row(0);
    const glm::vec4 r1 = row(1);
    const glm::vec4 r2 = row(2);
    const glm::vec4 r3 = row(3);

    std::array<glm::vec4, 6> planes = {
        r3 + r0, // Left
        r3 - r0, // Right
        r3 + r1, // Bottom
        r3 - r1, // Top
        r2,      // Far  (z >= 0 in [0, 1] Reversed-Z)
        r3 - r2, // Near (z <= w in [0, 1] Reversed-Z)
    };

    for (glm::vec4 &p : planes)
    {
        const float len = glm::length(glm::vec3(p));
        if (len > 1e-6f)
        {
            p /= len;
        }
    }
    return planes;
}

} // namespace

RenderResources::RenderResources()
{
    m_frameUbo = GpuBuffer::createPersistentMapped(sizeof(FrameUniforms));

    glCreateVertexArrays(1, &m_emptyVao);
    if (m_emptyVao == 0)
    {
        throw std::runtime_error("glCreateVertexArrays failed for fullscreen VAO");
    }

    createStarterMaterialArray();
}

RenderResources::~RenderResources()
{
    if (m_materialArrayTex != 0)
    {
        glDeleteTextures(1, &m_materialArrayTex);
        m_materialArrayTex = 0;
    }
    if (m_emptyVao != 0)
    {
        glDeleteVertexArrays(1, &m_emptyVao);
        m_emptyVao = 0;
    }
}

void RenderResources::onResize(int width, int height)
{
    width = std::max(1, width);
    height = std::max(1, height);
    if (width == m_width && height == m_height && m_gbufferFbo)
    {
        return;
    }
    m_width = width;
    m_height = height;

    m_gbuffer0.create2D(gl::Texture2DDesc{
        .width = m_width,
        .height = m_height,
        .internalFormat = GL_RGBA8,
        .format = GL_RGBA,
        .type = GL_UNSIGNED_BYTE,
        .minFilter = GL_NEAREST,
        .magFilter = GL_NEAREST,
        .wrapS = GL_CLAMP_TO_EDGE,
        .wrapT = GL_CLAMP_TO_EDGE,
    });
    m_gbuffer1.create2D(gl::Texture2DDesc{
        .width = m_width,
        .height = m_height,
        .internalFormat = GL_RGBA16F,
        .format = GL_RGBA,
        .type = GL_FLOAT,
        .minFilter = GL_NEAREST,
        .magFilter = GL_NEAREST,
        .wrapS = GL_CLAMP_TO_EDGE,
        .wrapT = GL_CLAMP_TO_EDGE,
    });
    m_depthTex.create2D(gl::Texture2DDesc{
        .width = m_width,
        .height = m_height,
        .internalFormat = GL_DEPTH_COMPONENT32F,
        .format = GL_DEPTH_COMPONENT,
        .type = GL_FLOAT,
        .minFilter = GL_NEAREST,
        .magFilter = GL_NEAREST,
        .wrapS = GL_CLAMP_TO_EDGE,
        .wrapT = GL_CLAMP_TO_EDGE,
    });
    m_hdrTex.create2D(gl::Texture2DDesc{
        .width = m_width,
        .height = m_height,
        .internalFormat = GL_RGBA16F,
        .format = GL_RGBA,
        .type = GL_FLOAT,
        .minFilter = GL_LINEAR,
        .magFilter = GL_LINEAR,
        .wrapS = GL_CLAMP_TO_EDGE,
        .wrapT = GL_CLAMP_TO_EDGE,
    });

    const GLenum gbufTargets[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    m_gbufferFbo = gl::Framebuffer::create();
    m_gbufferFbo.attachTexture2D(GL_COLOR_ATTACHMENT0, m_gbuffer0);
    m_gbufferFbo.attachTexture2D(GL_COLOR_ATTACHMENT1, m_gbuffer1);
    m_gbufferFbo.attachTexture2D(GL_DEPTH_ATTACHMENT, m_depthTex);
    m_gbufferFbo.setDrawBuffers(gbufTargets);
    m_gbufferFbo.ensureComplete();

    // Puddle modifier FBO intentionally omits depth attachment so depthTex can be sampled safely
    m_puddleFbo = gl::Framebuffer::create();
    m_puddleFbo.attachTexture2D(GL_COLOR_ATTACHMENT0, m_gbuffer0);
    m_puddleFbo.attachTexture2D(GL_COLOR_ATTACHMENT1, m_gbuffer1);
    m_puddleFbo.setDrawBuffers(gbufTargets);
    m_puddleFbo.ensureComplete();

    const GLenum hdrTargets[] = {GL_COLOR_ATTACHMENT0};
    m_hdrFbo = gl::Framebuffer::create();
    m_hdrFbo.attachTexture2D(GL_COLOR_ATTACHMENT0, m_hdrTex);
    m_hdrFbo.setDrawBuffers(hdrTargets);
    m_hdrFbo.ensureComplete();
}

void RenderResources::updateFrameUniforms(const RenderContext &ctx, std::uint32_t lightCount)
{
    const glm::mat4 vp = ctx.proj * ctx.view;

    FrameUniforms ubo;
    ubo.view = ctx.view;
    ubo.proj = ctx.proj;
    ubo.viewProj = vp;
    ubo.invViewProj = glm::inverse(vp);
    ubo.cameraPos = glm::vec4(ctx.cameraPos, ctx.nearPlane);
    ubo.frustumPlanes = extractReversedZFrustumPlanes(vp);
    ubo.screenParams = glm::vec4(
        static_cast<float>(ctx.viewportWidth),
        static_cast<float>(ctx.viewportHeight),
        1.0f / static_cast<float>(std::max(1, ctx.viewportWidth)),
        1.0f / static_cast<float>(std::max(1, ctx.viewportHeight)));
    ubo.timeParams = glm::vec4(ctx.elapsedTime, ctx.deltaTime, ctx.nearPlane, 0.0f);
    ubo.counts = glm::uvec4(m_scene.objectCount(), m_scene.meshCount(), lightCount, ctx.frameIndex);

    std::memcpy(m_frameUbo.mappedPtr(), &ubo, sizeof(FrameUniforms));
}

void RenderResources::drawFullscreenTriangle() const noexcept
{
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void RenderResources::createStarterMaterialArray()
{
    constexpr int texSize = 256;
    constexpr int layers = 4; // 0 = stone floor tiles, 1 = brick wall, 2 = tarnished gold, 3 = moss / vines

    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &m_materialArrayTex);
    glTextureStorage3D(m_materialArrayTex, 1, GL_RGBA8, texSize, texSize, layers);

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(texSize * texSize * 4));
    auto uploadLayer = [&](int layer, auto &&generator)
    {
        for (int y = 0; y < texSize; ++y)
        {
            for (int x = 0; x < texSize; ++x)
            {
                const glm::vec4 rgba = generator(x, y);
                const std::size_t idx = static_cast<std::size_t>((y * texSize + x) * 4);
                pixels[idx + 0] = static_cast<std::uint8_t>(std::clamp(rgba.r, 0.0f, 1.0f) * 255.0f);
                pixels[idx + 1] = static_cast<std::uint8_t>(std::clamp(rgba.g, 0.0f, 1.0f) * 255.0f);
                pixels[idx + 2] = static_cast<std::uint8_t>(std::clamp(rgba.b, 0.0f, 1.0f) * 255.0f);
                pixels[idx + 3] = static_cast<std::uint8_t>(std::clamp(rgba.a, 0.0f, 1.0f) * 255.0f);
            }
        }
        glTextureSubImage3D(m_materialArrayTex, 0, 0, 0, layer, texSize, texSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    };

    // Layer 0: Dungeon stone floor tiles (RGB = albedo, A = roughness)
    uploadLayer(0, [](int x, int y) -> glm::vec4
    {
        const bool grout = (x % 64 < 3) || (y % 64 < 3);
        const float grain = 0.04f * std::sin(static_cast<float>(x) * 0.25f) * std::cos(static_cast<float>(y) * 0.25f);
        const float base = grout ? 0.14f : (0.30f + grain);
        return glm::vec4(base * 0.92f, base * 0.95f, base * 1.02f, grout ? 0.95f : 0.78f);
    });

    // Layer 1: Alternating stone brick wall
    uploadLayer(1, [](int x, int y) -> glm::vec4
    {
        const int row = y / 32;
        const int xShifted = (x + (row % 2) * 32) % 64;
        const bool mortar = (xShifted < 4) || (y % 32 < 4);
        const float tone = mortar ? 0.16f : (0.38f + 0.04f * std::sin(static_cast<float>(x + row * 17) * 0.18f));
        return glm::vec4(tone * 1.05f, tone * 0.92f, tone * 0.80f, mortar ? 0.95f : 0.82f);
    });

    // Layer 2: Burnished brass / gold for SOR props
    uploadLayer(2, [](int x, int y) -> glm::vec4
    {
        const float sheen = 0.03f * std::sin(static_cast<float>(x) * 0.12f + static_cast<float>(y) * 0.05f);
        return glm::vec4(0.82f + sheen, 0.64f + sheen, 0.26f, 0.28f);
    });

    // Layer 3: Desaturated dark moss / dungeon vine green (uv.x encodes stem vs leaf tone)
    uploadLayer(3, [](int x, int y) -> glm::vec4
    {
        const float u = static_cast<float>(x) / 255.0f;
        const float v = static_cast<float>(y) / 255.0f;
        const float mottling = 0.02f * std::sin(u * 25.13f + v * 12.56f) * std::cos(v * 31.4f);
        const float stemToLeaf = std::clamp(u, 0.0f, 1.0f);
        const glm::vec3 stemCol(0.13f, 0.11f, 0.07f);
        const glm::vec3 leafCol(0.14f, 0.27f, 0.09f);
        const glm::vec3 c = glm::mix(stemCol, leafCol, stemToLeaf) + mottling;
        return glm::vec4(c, 0.72f);
    });

    glTextureParameteri(m_materialArrayTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_materialArrayTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_materialArrayTex, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_materialArrayTex, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

} // namespace game::render
