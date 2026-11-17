#include "game/render/RenderResources.hpp"
#include "game/TextureGenerator.hpp"

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
    m_lightsSSBO = GpuBuffer::createDynamic(sizeof(GpuPointLight));
    m_clusterCountsSSBO = GpuBuffer::createImmutable(kTotalClusters * sizeof(std::uint32_t));
    m_clusterIndicesSSBO = GpuBuffer::createImmutable(kTotalClusters * kMaxLightsPerCluster * sizeof(std::uint32_t));

    glCreateVertexArrays(1, &m_emptyVao);
    if (m_emptyVao == 0)
    {
        throw std::runtime_error("glCreateVertexArrays failed for fullscreen VAO");
    }

    createStarterMaterialArrays();
}

RenderResources::~RenderResources()
{
    if (m_albedoArrayTex != 0)
    {
        glDeleteTextures(1, &m_albedoArrayTex);
        m_albedoArrayTex = 0;
    }
    if (m_ormArrayTex != 0)
    {
        glDeleteTextures(1, &m_ormArrayTex);
        m_ormArrayTex = 0;
    }
    if (m_normalArrayTex != 0)
    {
        glDeleteTextures(1, &m_normalArrayTex);
        m_normalArrayTex = 0;
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

void RenderResources::setPointLights(std::span<const GpuPointLight> lights)
{
    m_lightCount = static_cast<std::uint32_t>(lights.size());
    const std::size_t requiredBytes = std::max<std::size_t>(sizeof(GpuPointLight), lights.size_bytes());
    if (requiredBytes > m_lightsSSBO.sizeBytes())
    {
        m_lightsSSBO = GpuBuffer::createDynamic(requiredBytes, lights.data());
    }
    else if (!lights.empty())
    {
        m_lightsSSBO.upload(0, lights.size_bytes(), lights.data());
    }
}

void RenderResources::updatePointLight(std::uint32_t index, const GpuPointLight &light)
{
    if (index >= m_lightCount)
    {
        return;
    }
    m_lightsSSBO.upload(static_cast<std::size_t>(index) * sizeof(GpuPointLight), sizeof(GpuPointLight), &light);
}

void RenderResources::updateFrameUniforms(const RenderContext &ctx)
{
    const glm::mat4 vp = ctx.proj * ctx.view;

    FrameUniforms ubo;
    ubo.view = ctx.view;
    ubo.proj = ctx.proj;
    ubo.invProj = glm::inverse(ctx.proj);
    ubo.viewProj = vp;
    ubo.invViewProj = glm::inverse(vp);
    ubo.cameraPos = glm::vec4(ctx.cameraPos, ctx.nearPlane);
    ubo.frustumPlanes = extractReversedZFrustumPlanes(vp);
    ubo.screenParams = glm::vec4(
        static_cast<float>(ctx.viewportWidth),
        static_cast<float>(ctx.viewportHeight),
        1.0f / static_cast<float>(std::max(1, ctx.viewportWidth)),
        1.0f / static_cast<float>(std::max(1, ctx.viewportHeight)));
    ubo.timeParams = glm::vec4(ctx.elapsedTime, ctx.deltaTime, ctx.nearPlane, ctx.farPlane);
    ubo.counts = glm::uvec4(m_scene.objectCount(), m_scene.meshCount(), m_lightCount, ctx.frameIndex);
    ubo.clusterDims = glm::uvec4(kClusterCountX, kClusterCountY, kClusterCountZ, kMaxLightsPerCluster);

    std::memcpy(m_frameUbo.mappedPtr(), &ubo, sizeof(FrameUniforms));
}

void RenderResources::drawFullscreenTriangle() const noexcept
{
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void RenderResources::createStarterMaterialArrays()
{
    constexpr int srcSize = 256;
    constexpr int dstSize = kMaterialArraySize;
    constexpr int layers = kMaterialArrayLayers; // 0=floor, 1=brick, 2=gold, 3=vine, 4=sand, 5=water, 6=palm, 7=foliage

    auto createArray = [&](GLuint &texId)
    {
        glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &texId);
        glTextureStorage3D(texId, kMaterialMipLevels, GL_RGBA8, dstSize, dstSize, layers);
        glTextureParameteri(texId, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTextureParameteri(texId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(texId, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(texId, GL_TEXTURE_WRAP_T, GL_REPEAT);
    };
    createArray(m_albedoArrayTex);
    createArray(m_ormArrayTex);
    createArray(m_normalArrayTex);

    std::vector<glm::vec4> srcPixels(static_cast<std::size_t>(srcSize * srcSize));
    std::vector<std::uint8_t> albedoPixels(static_cast<std::size_t>(dstSize * dstSize * 4));
    std::vector<std::uint8_t> ormPixels(static_cast<std::size_t>(dstSize * dstSize * 4));
    std::vector<std::uint8_t> flatNormalPixels(static_cast<std::size_t>(dstSize * dstSize * 4));

    for (std::size_t i = 0; i < flatNormalPixels.size(); i += 4)
    {
        flatNormalPixels[i + 0] = 128;
        flatNormalPixels[i + 1] = 128;
        flatNormalPixels[i + 2] = 255;
        flatNormalPixels[i + 3] = 255;
    }

    auto uploadLayer = [&](int layer, auto &&generator)
    {
        for (int y = 0; y < srcSize; ++y)
        {
            for (int x = 0; x < srcSize; ++x)
            {
                srcPixels[static_cast<std::size_t>(y * srcSize + x)] = generator(x, y);
            }
        }

        const float scale = static_cast<float>(srcSize) / static_cast<float>(dstSize);
        for (int y = 0; y < dstSize; ++y)
        {
            const float fy = static_cast<float>(y) * scale;
            const int y0 = static_cast<int>(fy) % srcSize;
            const int y1 = (y0 + 1) % srcSize;
            const float ty = fy - std::floor(fy);
            for (int x = 0; x < dstSize; ++x)
            {
                const float fx = static_cast<float>(x) * scale;
                const int x0 = static_cast<int>(fx) % srcSize;
                const int x1 = (x0 + 1) % srcSize;
                const float tx = fx - std::floor(fx);

                const glm::vec4 c00 = srcPixels[static_cast<std::size_t>(y0 * srcSize + x0)];
                const glm::vec4 c10 = srcPixels[static_cast<std::size_t>(y0 * srcSize + x1)];
                const glm::vec4 c01 = srcPixels[static_cast<std::size_t>(y1 * srcSize + x0)];
                const glm::vec4 c11 = srcPixels[static_cast<std::size_t>(y1 * srcSize + x1)];
                const glm::vec4 rgba = glm::mix(glm::mix(c00, c10, tx), glm::mix(c01, c11, tx), ty);

                const std::size_t idx = static_cast<std::size_t>((y * dstSize + x) * 4);
                albedoPixels[idx + 0] = static_cast<std::uint8_t>(std::clamp(rgba.r, 0.0f, 1.0f) * 255.0f);
                albedoPixels[idx + 1] = static_cast<std::uint8_t>(std::clamp(rgba.g, 0.0f, 1.0f) * 255.0f);
                albedoPixels[idx + 2] = static_cast<std::uint8_t>(std::clamp(rgba.b, 0.0f, 1.0f) * 255.0f);
                albedoPixels[idx + 3] = 255;

                ormPixels[idx + 0] = 255; // AO = 1.0
                ormPixels[idx + 1] = static_cast<std::uint8_t>(std::clamp(rgba.a, 0.0f, 1.0f) * 255.0f);
                ormPixels[idx + 2] = 0;   // Metallic = 0.0
                ormPixels[idx + 3] = 255;
            }
        }
        glTextureSubImage3D(m_albedoArrayTex, 0, 0, 0, layer, dstSize, dstSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, albedoPixels.data());
        glTextureSubImage3D(m_ormArrayTex, 0, 0, 0, layer, dstSize, dstSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, ormPixels.data());
        glTextureSubImage3D(m_normalArrayTex, 0, 0, 0, layer, dstSize, dstSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, flatNormalPixels.data());
    };

    // Layer 0: Starter stone floor tiles (overwritten by baked Compute floor in initBakedMaterials)
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

    // Layer 4: Oasis sand bank (uv.x blends damp shore silt -> warm dry desert sand)
    uploadLayer(4, [](int x, int y) -> glm::vec4
    {
        const float u = static_cast<float>(x) / 255.0f;
        const float v = static_cast<float>(y) / 255.0f;
        const float grain = 0.025f * std::sin(u * 48.0f + v * 17.0f) * std::cos(v * 52.0f - u * 11.0f);
        const glm::vec3 wetShore(0.24f, 0.20f, 0.14f);
        const glm::vec3 warmSand(0.62f, 0.50f, 0.33f);
        const float blend = std::clamp(u, 0.0f, 1.0f);
        const glm::vec3 c = glm::mix(wetShore, warmSand, blend) + grain;
        const float roughness = glm::mix(0.42f, 0.92f, blend);
        return glm::vec4(c, roughness);
    });

    // Layer 5: Oasis water placeholder (WaterPass uses depth/Fresnel forward shading)
    uploadLayer(5, [](int x, int y) -> glm::vec4
    {
        const float u = static_cast<float>(x) / 255.0f;
        const float v = static_cast<float>(y) / 255.0f;
        const float caustic = 0.5f + 0.5f * std::sin(u * 18.849f + std::sin(v * 12.566f) * 1.6f) * std::cos(v * 18.849f);
        const glm::vec3 deepTeal(0.04f, 0.26f, 0.31f);
        const glm::vec3 aquaTint(0.11f, 0.45f, 0.47f);
        const glm::vec3 c = glm::mix(deepTeal, aquaTint, caustic * 0.55f);
        return glm::vec4(c, 0.10f);
    });

    // Layer 6: Palm trunk bark (u >= 0.35) & dark coconut husk (u < 0.35)
    uploadLayer(6, [](int x, int y) -> glm::vec4
    {
        const float u = static_cast<float>(x) / 255.0f;
        const float v = static_cast<float>(y) / 255.0f;
        if (u < 0.35f)
        {
            const float fiber = 0.02f * std::sin(u * 60.0f + v * 40.0f);
            return glm::vec4(0.16f + fiber, 0.10f + fiber * 0.6f, 0.055f, 0.86f);
        }
        const float ringBand = 0.5f + 0.5f * std::sin(v * 37.699f);
        const float barkFiber = 0.025f * std::sin(u * 75.0f + v * 12.0f);
        const glm::vec3 darkBark(0.20f, 0.14f, 0.09f);
        const glm::vec3 lightBark(0.36f, 0.27f, 0.18f);
        return glm::vec4(glm::mix(darkBark, lightBark, ringBand) + barkFiber, 0.88f);
    });

    // Layer 7: Tropical palm frond & fern foliage (u = 0 bright midrib -> u = 1 lush blade green)
    uploadLayer(7, [](int x, int y) -> glm::vec4
    {
        const float u = static_cast<float>(x) / 255.0f;
        const float v = static_cast<float>(y) / 255.0f;
        const float vein = 0.022f * std::sin(u * 50.0f + v * 14.0f);
        const glm::vec3 midribCol(0.28f, 0.42f, 0.12f);
        const glm::vec3 frondCol(0.11f, 0.33f, 0.10f);
        const float blend = std::clamp(u, 0.0f, 1.0f);
        return glm::vec4(glm::mix(midribCol, frondCol, blend) + vein, 0.56f);
    });

    glGenerateTextureMipmap(m_albedoArrayTex);
    glGenerateTextureMipmap(m_ormArrayTex);
    glGenerateTextureMipmap(m_normalArrayTex);
}

void RenderResources::initBakedMaterials(const std::filesystem::path &shaderDir)
{
    const PBRTextures floorTex = generateFloor(shaderDir, kMaterialArraySize);

    glCopyImageSubData(floorTex.albedo.id(), GL_TEXTURE_2D, 0, 0, 0, 0, m_albedoArrayTex, GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, kMaterialArraySize, kMaterialArraySize, 1);
    glCopyImageSubData(floorTex.orm.id(), GL_TEXTURE_2D, 0, 0, 0, 0, m_ormArrayTex, GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, kMaterialArraySize, kMaterialArraySize, 1);
    glCopyImageSubData(floorTex.normal.id(), GL_TEXTURE_2D, 0, 0, 0, 0, m_normalArrayTex, GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, kMaterialArraySize, kMaterialArraySize, 1);

    glGenerateTextureMipmap(m_albedoArrayTex);
    glGenerateTextureMipmap(m_ormArrayTex);
    glGenerateTextureMipmap(m_normalArrayTex);
}

} // namespace game::render
