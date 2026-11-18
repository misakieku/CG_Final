#include "game/render/RenderResources.hpp"
#include "game/TextureGenerator.hpp"

#include <glm/gtc/matrix_transform.hpp>

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

[[nodiscard]] std::array<glm::vec4, 6> extractZeroToOneFrustumPlanes(const glm::mat4 &vp) noexcept
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

    // Single high-res directional shadow map with hardware bilinear PCF comparison
    m_shadowDepthTex.create2D(
        gl::Texture2DDesc{
            .width = kShadowMapSize,
            .height = kShadowMapSize,
            .internalFormat = GL_DEPTH_COMPONENT32F,
            .format = GL_DEPTH_COMPONENT,
            .type = GL_FLOAT,
            .minFilter = GL_LINEAR,
            .magFilter = GL_LINEAR,
            .wrapS = GL_CLAMP_TO_BORDER,
            .wrapT = GL_CLAMP_TO_BORDER,
        },
        nullptr);
    m_shadowDepthTex.setBorderColor(glm::vec4(1.0f));
    glTextureParameteri(m_shadowDepthTex.id(), GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(m_shadowDepthTex.id(), GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

    m_shadowFbo = gl::Framebuffer::create();
    m_shadowFbo.attachTexture2D(GL_DEPTH_ATTACHMENT, m_shadowDepthTex);
    m_shadowFbo.setNoColorBuffer();
    m_shadowFbo.ensureComplete();

    createStarterMaterialArrays();
}

RenderResources::~RenderResources()
{
    auto delTex = [](GLuint &id)
    {
        if (id != 0)
        {
            glDeleteTextures(1, &id);
            id = 0;
        }
    };
    delTex(m_albedoArrayTex);
    delTex(m_ormArrayTex);
    delTex(m_normalArrayTex);
    delTex(m_mazeGridTex);
    for (GLuint &sh : m_giShTex)
    {
        delTex(sh);
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
    ubo.frustumPlanes = extractZeroToOneFrustumPlanes(vp);
    ubo.screenParams = glm::vec4(
        static_cast<float>(ctx.viewportWidth),
        static_cast<float>(ctx.viewportHeight),
        1.0f / static_cast<float>(std::max(1, ctx.viewportWidth)),
        1.0f / static_cast<float>(std::max(1, ctx.viewportHeight)));
    ubo.timeParams = glm::vec4(ctx.elapsedTime, ctx.deltaTime, ctx.nearPlane, ctx.farPlane);
    ubo.counts = glm::uvec4(m_scene.objectCount(), m_scene.meshCount(), m_lightCount, ctx.frameIndex);
    ubo.clusterDims = glm::uvec4(kClusterCountX, kClusterCountY, kClusterCountZ, kMaxLightsPerCluster);

    // Texel-snapped directional moonlight orthographic matrix tracking the camera
    const glm::vec3 lightDir = glm::normalize(glm::vec3(0.45f, 0.85f, 0.35f));
    const glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), -lightDir, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 camLS = glm::vec3(lightView * glm::vec4(ctx.cameraPos.x, 0.0f, ctx.cameraPos.z, 1.0f));
    const float texelWorld = (2.0f * kShadowHalfExtent) / static_cast<float>(kShadowMapSize);
    const float snappedX = std::floor(camLS.x / texelWorld) * texelWorld;
    const float snappedY = std::floor(camLS.y / texelWorld) * texelWorld;
    const glm::mat4 lightProj = glm::orthoRH_ZO(
        snappedX - kShadowHalfExtent, snappedX + kShadowHalfExtent,
        snappedY - kShadowHalfExtent, snappedY + kShadowHalfExtent,
        -(camLS.z + 32.0f), -(camLS.z - 32.0f));

    ubo.lightViewProj = lightProj * lightView;
    ubo.shadowFrustumPlanes = extractZeroToOneFrustumPlanes(ubo.lightViewProj);
    ubo.shadowParams = glm::vec4(1.0f / static_cast<float>(kShadowMapSize), kShadowHalfExtent, 1.0f, 0.0f);

    if (hasGiGrid())
    {
        const float stepXZ = m_giCellSize * 0.5f;
        const float stepY = m_giWallHeight * 0.25f;
        ubo.giGridOrigin = glm::vec4(stepXZ * 0.5f, stepY * 0.5f, stepXZ * 0.5f, stepXZ);
        ubo.giGridInvStep = glm::vec4(1.0f / stepXZ, 1.0f / stepY, 1.0f / stepXZ, m_giCellSize);
        ubo.giDims = glm::uvec4(m_giProbeDims.x, m_giProbeDims.y, m_giProbeDims.z, m_giProbeDims.x * m_giProbeDims.y * m_giProbeDims.z);
        ubo.giMisc = glm::vec4(m_giWallHeight, static_cast<float>(m_giMapSize.x), static_cast<float>(m_giMapSize.y), 0.68f);
    }

    std::memcpy(m_frameUbo.mappedPtr(), &ubo, sizeof(FrameUniforms));
}

void RenderResources::initGiGrid(glm::ivec2 gridSize, float cellSize, float wallHeight, std::span<const std::uint8_t> cellKinds)
{
    if (gridSize.x <= 0 || gridSize.y <= 0 || cellKinds.size() < static_cast<std::size_t>(gridSize.x * gridSize.y))
    {
        return;
    }

    if (m_mazeGridTex != 0)
    {
        glDeleteTextures(1, &m_mazeGridTex);
        m_mazeGridTex = 0;
    }
    for (GLuint &sh : m_giShTex)
    {
        if (sh != 0)
        {
            glDeleteTextures(1, &sh);
            sh = 0;
        }
    }

    m_giMapSize = gridSize;
    m_giCellSize = cellSize;
    m_giWallHeight = wallHeight;
    m_giProbeDims = glm::uvec3(static_cast<std::uint32_t>(gridSize.x * 2), 5u, static_cast<std::uint32_t>(gridSize.y * 2));

    glCreateTextures(GL_TEXTURE_2D, 1, &m_mazeGridTex);
    glTextureStorage2D(m_mazeGridTex, 1, GL_R8UI, gridSize.x, gridSize.y);
    glTextureParameteri(m_mazeGridTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_mazeGridTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(m_mazeGridTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_mazeGridTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(m_mazeGridTex, 0, 0, 0, gridSize.x, gridSize.y, GL_RED_INTEGER, GL_UNSIGNED_BYTE, cellKinds.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    const GLsizei nx = static_cast<GLsizei>(m_giProbeDims.x);
    const GLsizei ny = static_cast<GLsizei>(m_giProbeDims.y);
    const GLsizei nz = static_cast<GLsizei>(m_giProbeDims.z);
    const std::array<float, 4> zeroClear{0.0f, 0.0f, 0.0f, 0.0f};
    for (GLuint &sh : m_giShTex)
    {
        glCreateTextures(GL_TEXTURE_3D, 1, &sh);
        glTextureStorage3D(sh, 1, GL_RGBA16F, nx, ny, nz);
        glTextureParameteri(sh, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(sh, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(sh, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(sh, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(sh, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glClearTexImage(sh, 0, GL_RGBA, GL_FLOAT, zeroClear.data());
    }

    const std::size_t totalProbes = static_cast<std::size_t>(m_giProbeDims.x) * m_giProbeDims.y * m_giProbeDims.z;
    std::vector<std::uint32_t> probeMeta(totalProbes, 0u);
    for (std::uint32_t iz = 0; iz < m_giProbeDims.z; ++iz)
    {
        for (std::uint32_t iy = 0; iy < m_giProbeDims.y; ++iy)
        {
            for (std::uint32_t ix = 0; ix < m_giProbeDims.x; ++ix)
            {
                const int cx = static_cast<int>(ix / 2u);
                const int cz = static_cast<int>(iz / 2u);
                const std::uint8_t kind = cellKinds[static_cast<std::size_t>(cz * gridSize.x + cx)];
                const std::size_t idx = (static_cast<std::size_t>(iz) * m_giProbeDims.y + iy) * m_giProbeDims.x + ix;
                if (kind == 0u)
                {
                    probeMeta[idx] = 0u; // Solid wall interior
                }
                else if (iy == 4u && kind != 2u)
                {
                    probeMeta[idx] = 0u; // Above roof of non-courtyard cells
                }
                else
                {
                    probeMeta[idx] = 1u; // Valid, untouched so first frame blends with alpha = 1.0
                }
            }
        }
    }

    m_giProbeMetaSSBO = GpuBuffer::createDynamic(totalProbes * sizeof(std::uint32_t), probeMeta.data());
    m_giActiveFlagSSBO = GpuBuffer::createImmutable(totalProbes * sizeof(std::uint32_t));
    m_giActiveListSSBO = GpuBuffer::createImmutable(totalProbes * sizeof(std::uint32_t));
    m_giDispatchCmdSSBO = GpuBuffer::createDynamic(3 * sizeof(std::uint32_t));
    m_giStagingShSSBO = GpuBuffer::createImmutable(totalProbes * 3 * sizeof(glm::vec4));
    m_giOctCacheSSBO = GpuBuffer::createImmutable(totalProbes * 64 * sizeof(glm::uvec2));
    m_giOctCacheSSBO.clearUint32(0u);
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
    constexpr int layers = kMaterialArrayLayers; // 0=floor, 1=brick, 2=gold, 3=vine, 4=sand, 5=water, 6=palm, 7=foliage, 8=wood

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

    // Layer 8: Weathered timber plank grain along local U axis
    uploadLayer(8, [](int x, int y) -> glm::vec4
    {
        const float u = static_cast<float>(x) / 255.0f;
        const float v = static_cast<float>(y) / 255.0f;
        const float grain = 0.5f + 0.5f * std::sin(v * 58.0f + std::sin(u * 12.566f) * 2.8f);
        const float fine = 0.02f * std::sin(u * 30.0f + v * 120.0f);
        const glm::vec3 darkWood(0.14f, 0.09f, 0.055f);
        const glm::vec3 agedOak(0.26f, 0.17f, 0.10f);
        return glm::vec4(glm::mix(darkWood, agedOak, grain) + fine, 0.84f);
    });

    glGenerateTextureMipmap(m_albedoArrayTex);
    glGenerateTextureMipmap(m_ormArrayTex);
    glGenerateTextureMipmap(m_normalArrayTex);
}

void RenderResources::initBakedMaterials(const std::filesystem::path &shaderDir)
{
    auto uploadBakedLayer = [&](GLint layer, const PBRTextures &tex)
    {
        glCopyImageSubData(tex.albedo.id(), GL_TEXTURE_2D, 0, 0, 0, 0, m_albedoArrayTex, GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, kMaterialArraySize, kMaterialArraySize, 1);
        glCopyImageSubData(tex.orm.id(), GL_TEXTURE_2D, 0, 0, 0, 0, m_ormArrayTex, GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, kMaterialArraySize, kMaterialArraySize, 1);
        glCopyImageSubData(tex.normal.id(), GL_TEXTURE_2D, 0, 0, 0, 0, m_normalArrayTex, GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, kMaterialArraySize, kMaterialArraySize, 1);
    };

    uploadBakedLayer(0, generateFloor(shaderDir, kMaterialArraySize));
    uploadBakedLayer(1, generateWall(shaderDir, kMaterialArraySize));

    glGenerateTextureMipmap(m_albedoArrayTex);
    glGenerateTextureMipmap(m_ormArrayTex);
    glGenerateTextureMipmap(m_normalArrayTex);
}

} // namespace game::render
