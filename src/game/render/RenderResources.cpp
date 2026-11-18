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

    // 3D froxel volumetric fog ping-pong history + front-to-back accumulated scatter/transmittance
    const std::array<float, 4> zeroFog{0.0f, 0.0f, 0.0f, 0.0f};
    const std::array<float, 4> clearAccum{0.0f, 0.0f, 0.0f, 1.0f};
    auto createFog3D = [](GLuint &texId, const std::array<float, 4> &clearVal)
    {
        glCreateTextures(GL_TEXTURE_3D, 1, &texId);
        glTextureStorage3D(
            texId,
            1,
            GL_RGBA16F,
            static_cast<GLsizei>(kFogVolumeWidth),
            static_cast<GLsizei>(kFogVolumeHeight),
            static_cast<GLsizei>(kFogVolumeDepth));
        glTextureParameteri(texId, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(texId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(texId, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(texId, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(texId, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glClearTexImage(texId, 0, GL_RGBA, GL_FLOAT, clearVal.data());
    };
    createFog3D(m_fogVolumeTex[0], zeroFog);
    createFog3D(m_fogVolumeTex[1], zeroFog);
    createFog3D(m_fogAccumTex, clearAccum);

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
    for (GLuint &fog : m_fogVolumeTex)
    {
        delTex(fog);
    }
    delTex(m_fogAccumTex);
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
        ubo.giMisc = glm::vec4(m_giWallHeight, static_cast<float>(m_giMapSize.x), static_cast<float>(m_giMapSize.y), ctx.whiteboxMode ? 0.68f : 0.0f);
    }
    else
    {
        ubo.giMisc = glm::vec4(m_giWallHeight, 0.0f, 0.0f, ctx.whiteboxMode ? 0.68f : 0.0f);
    }

    ubo.prevViewProj = m_hasPrevViewProj ? m_prevViewProj : vp;
    ubo.fogParams = glm::vec4(ctx.fogDensity, ctx.fogAnisotropy, ctx.fogHeightFalloff, ctx.fogFar);
    ubo.fogDims = glm::uvec4(kFogVolumeWidth, kFogVolumeHeight, kFogVolumeDepth, 0u);
    m_prevViewProj = vp;
    m_hasPrevViewProj = true;

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
    m_giDispatchCmdSSBO = GpuBuffer::createDynamic(8 * sizeof(std::uint32_t));
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

    struct StarterTexel
    {
        glm::vec3 albedo{0.5f};
        float ao = 1.0f;
        float roughness = 0.8f;
        float metallic = 0.0f;
        float height = 0.5f;
    };

    std::vector<StarterTexel> srcTexels(static_cast<std::size_t>(srcSize * srcSize));
    std::vector<std::uint8_t> albedoPixels(static_cast<std::size_t>(dstSize * dstSize * 4));
    std::vector<std::uint8_t> ormPixels(static_cast<std::size_t>(dstSize * dstSize * 4));
    std::vector<std::uint8_t> normalPixels(static_cast<std::size_t>(dstSize * dstSize * 4));

    auto uploadLayer = [&](int layer, float normalStrength, auto &&generator)
    {
        for (int y = 0; y < srcSize; ++y)
        {
            for (int x = 0; x < srcSize; ++x)
            {
                srcTexels[static_cast<std::size_t>(y * srcSize + x)] = generator(x, y);
            }
        }

        const float scale = static_cast<float>(srcSize) / static_cast<float>(dstSize);
        for (int y = 0; y < dstSize; ++y)
        {
            const float fy = static_cast<float>(y) * scale;
            const int y0 = static_cast<int>(fy) % srcSize;
            const int y1 = (y0 + 1) % srcSize;
            const int yM = (y0 + srcSize - 1) % srcSize;
            const float ty = fy - std::floor(fy);
            for (int x = 0; x < dstSize; ++x)
            {
                const float fx = static_cast<float>(x) * scale;
                const int x0 = static_cast<int>(fx) % srcSize;
                const int x1 = (x0 + 1) % srcSize;
                const int xM = (x0 + srcSize - 1) % srcSize;
                const float tx = fx - std::floor(fx);

                const StarterTexel &t00 = srcTexels[static_cast<std::size_t>(y0 * srcSize + x0)];
                const StarterTexel &t10 = srcTexels[static_cast<std::size_t>(y0 * srcSize + x1)];
                const StarterTexel &t01 = srcTexels[static_cast<std::size_t>(y1 * srcSize + x0)];
                const StarterTexel &t11 = srcTexels[static_cast<std::size_t>(y1 * srcSize + x1)];

                auto bilerp = [&](float StarterTexel::*mem) -> float
                {
                    const float a = glm::mix(t00.*mem, t10.*mem, tx);
                    const float b = glm::mix(t01.*mem, t11.*mem, tx);
                    return glm::mix(a, b, ty);
                };

                const glm::vec3 cA = glm::mix(t00.albedo, t10.albedo, tx);
                const glm::vec3 cB = glm::mix(t01.albedo, t11.albedo, tx);
                const glm::vec3 albedo = glm::mix(cA, cB, ty);
                const float ao = bilerp(&StarterTexel::ao);
                const float roughness = bilerp(&StarterTexel::roughness);
                const float metallic = bilerp(&StarterTexel::metallic);
                const float height = bilerp(&StarterTexel::height);

                // Seamless central difference normal from scalar height field
                const float hL = srcTexels[static_cast<std::size_t>(y0 * srcSize + xM)].height;
                const float hR = srcTexels[static_cast<std::size_t>(y0 * srcSize + x1)].height;
                const float hD = srcTexels[static_cast<std::size_t>(yM * srcSize + x0)].height;
                const float hU = srcTexels[static_cast<std::size_t>(y1 * srcSize + x0)].height;
                const glm::vec3 nrm = glm::normalize(glm::vec3((hL - hR) * normalStrength, (hD - hU) * normalStrength, 1.0f));

                const std::size_t idx = static_cast<std::size_t>((y * dstSize + x) * 4);
                albedoPixels[idx + 0] = static_cast<std::uint8_t>(std::clamp(albedo.r, 0.0f, 1.0f) * 255.0f);
                albedoPixels[idx + 1] = static_cast<std::uint8_t>(std::clamp(albedo.g, 0.0f, 1.0f) * 255.0f);
                albedoPixels[idx + 2] = static_cast<std::uint8_t>(std::clamp(albedo.b, 0.0f, 1.0f) * 255.0f);
                albedoPixels[idx + 3] = 255;

                ormPixels[idx + 0] = static_cast<std::uint8_t>(std::clamp(ao, 0.0f, 1.0f) * 255.0f);
                ormPixels[idx + 1] = static_cast<std::uint8_t>(std::clamp(roughness, 0.0f, 1.0f) * 255.0f);
                ormPixels[idx + 2] = static_cast<std::uint8_t>(std::clamp(metallic, 0.0f, 1.0f) * 255.0f);
                ormPixels[idx + 3] = 255;

                normalPixels[idx + 0] = static_cast<std::uint8_t>(std::clamp(nrm.x * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
                normalPixels[idx + 1] = static_cast<std::uint8_t>(std::clamp(nrm.y * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
                normalPixels[idx + 2] = static_cast<std::uint8_t>(std::clamp(nrm.z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
                normalPixels[idx + 3] = static_cast<std::uint8_t>(std::clamp(height, 0.0f, 1.0f) * 255.0f);
            }
        }
        glTextureSubImage3D(m_albedoArrayTex, 0, 0, 0, layer, dstSize, dstSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, albedoPixels.data());
        glTextureSubImage3D(m_ormArrayTex, 0, 0, 0, layer, dstSize, dstSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, ormPixels.data());
        glTextureSubImage3D(m_normalArrayTex, 0, 0, 0, layer, dstSize, dstSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, normalPixels.data());
    };

    auto srgbToLin = [](glm::vec3 c) -> glm::vec3
    {
        return glm::pow(glm::max(c, glm::vec3(0.0f)), glm::vec3(2.2f));
    };

    // Layer 0 & Layer 1: placeholders immediately overwritten by GPU Compute Bake in initBakedMaterials()
    uploadLayer(0, 1.0f, [&](int, int) -> StarterTexel { return StarterTexel{.albedo = srgbToLin({0.36f, 0.38f, 0.40f}), .roughness = 0.72f}; });
    uploadLayer(1, 1.0f, [&](int, int) -> StarterTexel { return StarterTexel{.albedo = srgbToLin({0.46f, 0.48f, 0.49f}), .roughness = 0.80f}; });

    // Layer 2: Burnished antique metallic gold / brass with hammered surface relief for SOR relics
    uploadLayer(2, 2.4f, [&](int x, int y) -> StarterTexel
    {
        const float u = static_cast<float>(x) / 256.0f;
        const float v = static_cast<float>(y) / 256.0f;
        const float hammer = 0.5f + 0.5f * std::sin(u * 62.8318f + std::sin(v * 31.4159f) * 1.4f) * std::cos(v * 62.8318f);
        const float brush = 0.5f + 0.5f * std::sin(u * 25.1327f + v * 100.531f);
        const float patina = 0.5f + 0.5f * std::sin(u * 12.5663f - v * 18.8495f);
        const float height = 0.5f + (hammer - 0.5f) * 0.25f + (brush - 0.5f) * 0.10f;
        const glm::vec3 brightGold(0.92f, 0.74f, 0.32f);
        const glm::vec3 tarnishedGold(0.58f, 0.44f, 0.18f);
        const glm::vec3 col = srgbToLin(glm::mix(tarnishedGold, brightGold, 0.65f + 0.35f * patina) * (0.94f + 0.10f * brush));
        return StarterTexel{
            .albedo = col,
            .ao = 0.72f + 0.28f * hammer,
            .roughness = 0.20f + 0.16f * (1.0f - brush) + 0.08f * (1.0f - patina),
            .metallic = 0.94f,
            .height = height,
        };
    });

    // Layer 3: Stylized damp dungeon ivy vine & leaves (linear greens matching wall stone GI balance)
    uploadLayer(3, 2.8f, [&](int x, int y) -> StarterTexel
    {
        const float u = static_cast<float>(x) / 256.0f;
        const float v = static_cast<float>(y) / 256.0f;
        const float veinWave = 0.5f + 0.5f * std::sin(v * 50.2654f + std::abs(u - 0.5f) * 18.8495f);
        const float midrib = std::exp(-std::pow((v - 0.5f) * 12.0f, 2.0f));
        const float mottling = 0.5f + 0.5f * std::sin(u * 25.1327f + v * 12.5663f) * std::cos(v * 31.4159f);
        const float stemToLeaf = std::clamp((u - 0.08f) * 1.45f, 0.0f, 1.0f);
        const glm::vec3 stemCol(0.24f, 0.17f, 0.11f);
        const glm::vec3 deepLeaf(0.16f, 0.34f, 0.13f);
        const glm::vec3 midribLeaf(0.29f, 0.52f, 0.19f);
        const glm::vec3 leafCol = glm::mix(deepLeaf, midribLeaf, std::clamp(midrib * 0.65f + veinWave * 0.20f + mottling * 0.15f, 0.0f, 1.0f));
        const glm::vec3 col = glm::mix(stemCol, leafCol, stemToLeaf);
        const float height = 0.45f + stemToLeaf * (midrib * 0.30f + veinWave * 0.16f);
        return StarterTexel{
            .albedo = col,
            .ao = 0.78f + 0.22f * veinWave,
            .roughness = glm::mix(0.84f, 0.46f + 0.14f * (1.0f - midrib), stemToLeaf),
            .metallic = 0.0f,
            .height = height,
        };
    });

    // Layer 4..7: Oasis & foliage layers (preserved cleanly with periodic normals)
    uploadLayer(4, 1.2f, [&](int x, int y) -> StarterTexel
    {
        const float u = static_cast<float>(x) / 256.0f;
        const float v = static_cast<float>(y) / 256.0f;
        const float grain = 0.5f + 0.5f * std::sin(u * 50.265f + v * 18.849f) * std::cos(v * 50.265f);
        const glm::vec3 col = srgbToLin(glm::mix(glm::vec3(0.24f, 0.20f, 0.14f), glm::vec3(0.56f, 0.46f, 0.31f), u));
        return StarterTexel{.albedo = col, .roughness = glm::mix(0.45f, 0.90f, u), .height = 0.4f + 0.2f * grain};
    });
    uploadLayer(5, 0.8f, [&](int x, int y) -> StarterTexel
    {
        const float u = static_cast<float>(x) / 256.0f;
        const float v = static_cast<float>(y) / 256.0f;
        const float caustic = 0.5f + 0.5f * std::sin(u * 18.849f + std::sin(v * 12.566f) * 1.6f) * std::cos(v * 18.849f);
        return StarterTexel{.albedo = srgbToLin(glm::mix(glm::vec3(0.05f, 0.24f, 0.28f), glm::vec3(0.11f, 0.40f, 0.42f), caustic * 0.55f)), .roughness = 0.12f, .height = 0.5f + 0.1f * caustic};
    });
    uploadLayer(6, 2.0f, [&](int x, int y) -> StarterTexel
    {
        const float u = static_cast<float>(x) / 256.0f;
        const float v = static_cast<float>(y) / 256.0f;
        const float ringBand = 0.5f + 0.5f * std::sin(v * 37.699f);
        const float bark = 0.5f + 0.5f * std::sin(u * 75.398f + v * 12.566f);
        return StarterTexel{.albedo = srgbToLin(glm::mix(glm::vec3(0.20f, 0.14f, 0.09f), glm::vec3(0.34f, 0.25f, 0.17f), ringBand)), .roughness = 0.86f, .height = 0.4f + 0.25f * bark};
    });
    uploadLayer(7, 2.2f, [&](int x, int y) -> StarterTexel
    {
        const float u = static_cast<float>(x) / 256.0f;
        const float v = static_cast<float>(y) / 256.0f;
        const float vein = 0.5f + 0.5f * std::sin(u * 50.265f + v * 12.566f);
        return StarterTexel{.albedo = srgbToLin(glm::mix(glm::vec3(0.22f, 0.34f, 0.11f), glm::vec3(0.11f, 0.28f, 0.10f), u)), .roughness = 0.54f, .height = 0.45f + 0.2f * vein};
    });

    // Layer 8: placeholder overwritten by GPU Compute Wood Bake in initBakedMaterials()
    uploadLayer(8, 1.0f, [&](int, int) -> StarterTexel { return StarterTexel{.albedo = srgbToLin({0.22f, 0.17f, 0.13f}), .roughness = 0.80f}; });

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
    uploadBakedLayer(8, generateWood(shaderDir, kMaterialArraySize));

    glGenerateTextureMipmap(m_albedoArrayTex);
    glGenerateTextureMipmap(m_ormArrayTex);
    glGenerateTextureMipmap(m_normalArrayTex);
}

} // namespace game::render
