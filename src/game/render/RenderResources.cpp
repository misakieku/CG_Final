#include "game/render/RenderResources.hpp"
#include "game/TextureGenerator.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <bit>
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

[[nodiscard]] int prevPow2(int v) noexcept
{
    int p = 1;
    while ((p << 1) <= v)
    {
        p <<= 1;
    }
    return p;
}

[[nodiscard]] glm::ivec2 chooseHzbExtent(int width, int height) noexcept
{
    constexpr int kMinDim = 64;
    constexpr std::int64_t kMaxPixels = 262144;
    int w = std::clamp(prevPow2(std::max(1, width / 2)), kMinDim, 1024);
    int h = std::clamp(prevPow2(std::max(1, height / 2)), kMinDim, 1024);
    const float targetAspect = static_cast<float>(std::max(1, width)) / static_cast<float>(std::max(1, height));

    while (static_cast<std::int64_t>(w) * h > kMaxPixels && (w > kMinDim || h > kMinDim))
    {
        if (w <= kMinDim)
        {
            h >>= 1;
            continue;
        }
        if (h <= kMinDim)
        {
            w >>= 1;
            continue;
        }
        const float aspectHalveW = static_cast<float>(w >> 1) / static_cast<float>(h);
        const float aspectHalveH = static_cast<float>(w) / static_cast<float>(h >> 1);
        const float errW = std::max(aspectHalveW / targetAspect, targetAspect / aspectHalveW);
        const float errH = std::max(aspectHalveH / targetAspect, targetAspect / aspectHalveH);
        if (errW <= errH)
        {
            w >>= 1;
        }
        else
        {
            h >>= 1;
        }
    }
    return glm::ivec2(w, h);
}

[[nodiscard]] int computeMipCount(glm::ivec2 size) noexcept
{
    int maxDim = std::max(1, std::max(size.x, size.y));
    int mips = 1;
    while (maxDim > 1)
    {
        maxDim >>= 1;
        ++mips;
    }
    return mips;
}

} // namespace

RenderResources::RenderResources(bool deferMaterialGen)
{
    for (GpuBuffer &ubo : m_frameUboRing)
    {
        ubo = GpuBuffer::createPersistentMapped(sizeof(FrameUniforms));
    }
    m_lightsSSBO = GpuBuffer::createDynamic(sizeof(GpuPointLight));
    m_flamesSSBO = GpuBuffer::createDynamic(sizeof(FlameInstance));
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

    // Top-4 cached omnidirectional point shadow cubemap array (256x256 x 6 faces x 4 slots)
    glCreateTextures(GL_TEXTURE_CUBE_MAP_ARRAY, 1, &m_pointShadowCubeArray);
    glTextureStorage3D(
        m_pointShadowCubeArray,
        1,
        GL_DEPTH_COMPONENT32F,
        kPointShadowSize,
        kPointShadowSize,
        kMaxPointShadowCasters * 6);
    glTextureParameteri(m_pointShadowCubeArray, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_pointShadowCubeArray, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_pointShadowCubeArray, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_pointShadowCubeArray, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_pointShadowCubeArray, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_pointShadowCubeArray, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(m_pointShadowCubeArray, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    const float oneDepth = 1.0f;
    glClearTexImage(m_pointShadowCubeArray, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &oneDepth);

    m_pointShadowFbo = gl::Framebuffer::create();
    m_pointShadowFbo.bind();
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_pointShadowCubeArray, 0, 0);
    m_pointShadowFbo.setNoColorBuffer();
    m_pointShadowFbo.ensureComplete();

    // 1x1 neutral fallback textures so disabled passes can be skipped without shader branches
    glCreateTextures(GL_TEXTURE_2D, 1, &m_dummyShadowTex);
    glTextureStorage2D(m_dummyShadowTex, 1, GL_DEPTH_COMPONENT32F, 1, 1);
    glTextureParameteri(m_dummyShadowTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_dummyShadowTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_dummyShadowTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_dummyShadowTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_dummyShadowTex, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(m_dummyShadowTex, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glClearTexImage(m_dummyShadowTex, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &oneDepth);

    glCreateTextures(GL_TEXTURE_CUBE_MAP_ARRAY, 1, &m_dummyPointShadowTex);
    glTextureStorage3D(m_dummyPointShadowTex, 1, GL_DEPTH_COMPONENT32F, 1, 1, 6);
    glTextureParameteri(m_dummyPointShadowTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_dummyPointShadowTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_dummyPointShadowTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_dummyPointShadowTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_dummyPointShadowTex, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_dummyPointShadowTex, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(m_dummyPointShadowTex, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glClearTexImage(m_dummyPointShadowTex, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &oneDepth);

    const std::uint8_t whiteByte = 255u;
    glCreateTextures(GL_TEXTURE_2D, 1, &m_dummyWhiteR8Tex);
    glTextureStorage2D(m_dummyWhiteR8Tex, 1, GL_R8, 1, 1);
    glTextureParameteri(m_dummyWhiteR8Tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_dummyWhiteR8Tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glClearTexImage(m_dummyWhiteR8Tex, 0, GL_RED, GL_UNSIGNED_BYTE, &whiteByte);

    const std::array<float, 4> blackZero{0.0f, 0.0f, 0.0f, 0.0f};
    glCreateTextures(GL_TEXTURE_2D, 1, &m_dummyBlackTex);
    glTextureStorage2D(m_dummyBlackTex, 1, GL_R11F_G11F_B10F, 1, 1);
    glTextureParameteri(m_dummyBlackTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_dummyBlackTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glClearTexImage(m_dummyBlackTex, 0, GL_RGB, GL_FLOAT, blackZero.data());

    glCreateTextures(GL_TEXTURE_3D, 1, &m_dummyZero3DTex);
    glTextureStorage3D(m_dummyZero3DTex, 1, GL_RGBA16F, 1, 1, 1);
    glTextureParameteri(m_dummyZero3DTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_dummyZero3DTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glClearTexImage(m_dummyZero3DTex, 0, GL_RGBA, GL_FLOAT, blackZero.data());

    const std::array<float, 4> neutralFog{0.0f, 0.0f, 0.0f, 1.0f};
    glCreateTextures(GL_TEXTURE_3D, 1, &m_dummyNeutralFog3DTex);
    glTextureStorage3D(m_dummyNeutralFog3DTex, 1, GL_RGBA16F, 1, 1, 1);
    glTextureParameteri(m_dummyNeutralFog3DTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_dummyNeutralFog3DTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glClearTexImage(m_dummyNeutralFog3DTex, 0, GL_RGBA, GL_FLOAT, neutralFog.data());

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

    if (!deferMaterialGen)
    {
        createStarterMaterialArrays();
    }
}

RenderResources::~RenderResources()
{
    for (GLsync &fence : m_frameUboFences)
    {
        if (fence != nullptr)
        {
            glDeleteSync(fence);
            fence = nullptr;
        }
    }
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
    for (GLuint &hzb : m_hzbTex)
    {
        delTex(hzb);
    }
    delTex(m_bloomTex);
    delTex(m_pointShadowCubeArray);
    delTex(m_dummyShadowTex);
    delTex(m_dummyPointShadowTex);
    delTex(m_dummyWhiteR8Tex);
    delTex(m_dummyBlackTex);
    delTex(m_dummyZero3DTex);
    delTex(m_dummyNeutralFog3DTex);
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
    m_aoWidth = std::max(1, (width + 1) / 2);
    m_aoHeight = std::max(1, (height + 1) / 2);

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
    m_ssprHashTex.create2D(gl::Texture2DDesc{
        .width = m_width,
        .height = m_height,
        .internalFormat = GL_R32UI,
        .format = GL_RED_INTEGER,
        .type = GL_UNSIGNED_INT,
        .minFilter = GL_NEAREST,
        .magFilter = GL_NEAREST,
        .wrapS = GL_CLAMP_TO_EDGE,
        .wrapT = GL_CLAMP_TO_EDGE,
    });
    m_ssprReflectTex.create2D(gl::Texture2DDesc{
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
    m_ldrTex.create2D(gl::Texture2DDesc{
        .width = m_width,
        .height = m_height,
        .internalFormat = GL_RGBA8,
        .format = GL_RGBA,
        .type = GL_UNSIGNED_BYTE,
        .minFilter = GL_LINEAR,
        .magFilter = GL_LINEAR,
        .wrapS = GL_CLAMP_TO_EDGE,
        .wrapT = GL_CLAMP_TO_EDGE,
    });
    m_aoRawTex.create2D(gl::Texture2DDesc{
        .width = m_aoWidth,
        .height = m_aoHeight,
        .internalFormat = GL_R8,
        .format = GL_RED,
        .type = GL_UNSIGNED_BYTE,
        .minFilter = GL_LINEAR,
        .magFilter = GL_LINEAR,
        .wrapS = GL_CLAMP_TO_EDGE,
        .wrapT = GL_CLAMP_TO_EDGE,
    });
    m_aoTex.create2D(gl::Texture2DDesc{
        .width = m_aoWidth,
        .height = m_aoHeight,
        .internalFormat = GL_R8,
        .format = GL_RED,
        .type = GL_UNSIGNED_BYTE,
        .minFilter = GL_LINEAR,
        .magFilter = GL_LINEAR,
        .wrapS = GL_CLAMP_TO_EDGE,
        .wrapT = GL_CLAMP_TO_EDGE,
    });
    m_giScreenTex.create2D(gl::Texture2DDesc{
        .width = m_aoWidth,
        .height = m_aoHeight,
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

    const GLenum singleTarget[] = {GL_COLOR_ATTACHMENT0};
    m_aoRawFbo = gl::Framebuffer::create();
    m_aoRawFbo.attachTexture2D(GL_COLOR_ATTACHMENT0, m_aoRawTex);
    m_aoRawFbo.setDrawBuffers(singleTarget);
    m_aoRawFbo.ensureComplete();

    m_aoFbo = gl::Framebuffer::create();
    m_aoFbo.attachTexture2D(GL_COLOR_ATTACHMENT0, m_aoTex);
    m_aoFbo.setDrawBuffers(singleTarget);
    m_aoFbo.ensureComplete();

    m_giScreenFbo = gl::Framebuffer::create();
    m_giScreenFbo.attachTexture2D(GL_COLOR_ATTACHMENT0, m_giScreenTex);
    m_giScreenFbo.setDrawBuffers(singleTarget);
    m_giScreenFbo.ensureComplete();

    m_hdrFbo = gl::Framebuffer::create();
    m_hdrFbo.attachTexture2D(GL_COLOR_ATTACHMENT0, m_hdrTex);
    m_hdrFbo.setDrawBuffers(singleTarget);
    m_hdrFbo.ensureComplete();

    m_hdrDepthFbo = gl::Framebuffer::create();
    m_hdrDepthFbo.attachTexture2D(GL_COLOR_ATTACHMENT0, m_hdrTex);
    m_hdrDepthFbo.attachTexture2D(GL_DEPTH_ATTACHMENT, m_depthTex);
    m_hdrDepthFbo.setDrawBuffers(singleTarget);
    m_hdrDepthFbo.ensureComplete();

    m_ldrFbo = gl::Framebuffer::create();
    m_ldrFbo.attachTexture2D(GL_COLOR_ATTACHMENT0, m_ldrTex);
    m_ldrFbo.setDrawBuffers(singleTarget);
    m_ldrFbo.ensureComplete();

    m_hzbSize = chooseHzbExtent(m_width, m_height);
    m_hzbMipCount = computeMipCount(m_hzbSize);
    m_hzbValid = false;
    const float zeroDepth = 0.0f;
    for (GLuint &hzb : m_hzbTex)
    {
        if (hzb != 0)
        {
            glDeleteTextures(1, &hzb);
            hzb = 0;
        }
        glCreateTextures(GL_TEXTURE_2D, 1, &hzb);
        glTextureStorage2D(hzb, m_hzbMipCount, GL_R32F, m_hzbSize.x, m_hzbSize.y);
        glTextureParameteri(hzb, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        glTextureParameteri(hzb, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(hzb, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(hzb, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(hzb, GL_TEXTURE_MAX_LEVEL, m_hzbMipCount - 1);
        for (int mip = 0; mip < m_hzbMipCount; ++mip)
        {
            glClearTexImage(hzb, mip, GL_RED, GL_FLOAT, &zeroDepth);
        }
    }

    // Half-res R11F_G11F_B10F mip pyramid for Jimenez 13-tap downsample / 9-tap tent upsample HDR bloom
    m_bloomSize = glm::ivec2(std::max(1, (m_width + 1) / 2), std::max(1, (m_height + 1) / 2));
    m_bloomMipCount = std::min(6, computeMipCount(m_bloomSize));
    if (m_bloomTex != 0)
    {
        glDeleteTextures(1, &m_bloomTex);
        m_bloomTex = 0;
    }
    glCreateTextures(GL_TEXTURE_2D, 1, &m_bloomTex);
    glTextureStorage2D(m_bloomTex, m_bloomMipCount, GL_R11F_G11F_B10F, m_bloomSize.x, m_bloomSize.y);
    glTextureParameteri(m_bloomTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    glTextureParameteri(m_bloomTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_bloomTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_bloomTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_bloomTex, GL_TEXTURE_MAX_LEVEL, m_bloomMipCount - 1);

    m_bloomFbo = gl::Framebuffer::create();
    m_bloomFbo.bind();
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bloomTex, 0);
    m_bloomFbo.setDrawBuffers(singleTarget);
    m_bloomFbo.ensureComplete();
}

void RenderResources::setPointLights(std::span<const GpuPointLight> lights, std::size_t shadowCasterCount)
{
    m_lightCount = static_cast<std::uint32_t>(lights.size());
    m_cpuLights.assign(lights.begin(), lights.end());
    m_shadowCasterLightCount = std::min(shadowCasterCount, m_cpuLights.size());
    invalidatePointShadows();

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

void RenderResources::setFlames(std::span<const FlameInstance> flames)
{
    m_flameCount = static_cast<std::uint32_t>(flames.size());
    const std::size_t requiredBytes = std::max<std::size_t>(sizeof(FlameInstance), flames.size_bytes());
    if (requiredBytes > m_flamesSSBO.sizeBytes())
    {
        m_flamesSSBO = GpuBuffer::createDynamic(requiredBytes, flames.data());
    }
    else if (!flames.empty())
    {
        m_flamesSSBO.upload(0, flames.size_bytes(), flames.data());
    }
}

void RenderResources::updatePointLight(std::uint32_t index, const GpuPointLight &light)
{
    if (index >= m_lightCount)
    {
        return;
    }
    m_cpuLights[index] = light;
    for (PointShadowSlot &slot : m_pointShadowSlots)
    {
        if (slot.lightIndex == static_cast<int>(index))
        {
            const glm::vec3 newPos(light.positionRadius);
            const float newRadius = light.positionRadius.w;
            if (glm::distance(slot.pos, newPos) > 1e-4f || std::abs(slot.radius - newRadius) > 1e-4f)
            {
                slot.pos = newPos;
                slot.radius = newRadius;
                slot.dirty = true;
            }
        }
    }
    m_lightsSSBO.upload(static_cast<std::size_t>(index) * sizeof(GpuPointLight), sizeof(GpuPointLight), &light);
}

void RenderResources::invalidatePointShadows() noexcept
{
    for (PointShadowSlot &slot : m_pointShadowSlots)
    {
        slot = PointShadowSlot{};
    }
    m_pointShadowBurst = true;
}

void RenderResources::updatePointShadowSlots(const glm::vec3 &cameraPos, bool allowMultiSwap)
{
    if (m_shadowCasterLightCount == 0 || m_cpuLights.empty())
    {
        for (PointShadowSlot &slot : m_pointShadowSlots)
        {
            slot.lightIndex = -1;
            slot.fade = 0.0f;
            slot.dirty = false;
        }
        return;
    }

    struct LightCandidate
    {
        int index = 0;
        float dist = 0.0f;
        float rankDist = 0.0f;
    };

    auto isCached = [&](int lightIdx) noexcept -> bool
    {
        for (const PointShadowSlot &slot : m_pointShadowSlots)
        {
            if (slot.lightIndex == lightIdx)
            {
                return true;
            }
        }
        return false;
    };

    std::vector<LightCandidate> sorted;
    sorted.reserve(m_shadowCasterLightCount);
    for (std::size_t i = 0; i < m_shadowCasterLightCount; ++i)
    {
        const glm::vec3 pos(m_cpuLights[i].positionRadius);
        const float dist = glm::distance(pos, cameraPos);
        const float hysteresis = isCached(static_cast<int>(i)) ? 0.85f : 0.0f;
        sorted.push_back({static_cast<int>(i), dist, dist - hysteresis});
    }

    std::sort(sorted.begin(), sorted.end(), [](const LightCandidate &a, const LightCandidate &b)
    {
        return a.dist < b.dist;
    });

    const float fifthDist = (sorted.size() > static_cast<std::size_t>(kMaxPointShadowCasters))
                                ? sorted[static_cast<std::size_t>(kMaxPointShadowCasters)].dist
                                : 1e6f;

    std::sort(sorted.begin(), sorted.end(), [](const LightCandidate &a, const LightCandidate &b)
    {
        return a.rankDist < b.rankDist;
    });

    constexpr float kMaxShadowRange = 28.0f;
    std::array<int, kMaxPointShadowCasters> desired{-1, -1, -1, -1};
    int desiredCount = 0;
    for (const LightCandidate &cand : sorted)
    {
        if (desiredCount >= kMaxPointShadowCasters || cand.dist > kMaxShadowRange)
        {
            break;
        }
        desired[static_cast<std::size_t>(desiredCount++)] = cand.index;
    }

    auto isDesired = [&](int lightIdx) noexcept -> bool
    {
        for (int i = 0; i < desiredCount; ++i)
        {
            if (desired[static_cast<std::size_t>(i)] == lightIdx)
            {
                return true;
            }
        }
        return false;
    };

    for (int i = 0; i < desiredCount; ++i)
    {
        const int targetIdx = desired[static_cast<std::size_t>(i)];
        if (isCached(targetIdx))
        {
            continue;
        }

        // Prefer empty slot first, otherwise evict the farthest non-desired slot
        int replaceSlot = -1;
        float farthestDist = -1.0f;
        for (int s = 0; s < kMaxPointShadowCasters; ++s)
        {
            const PointShadowSlot &slot = m_pointShadowSlots[static_cast<std::size_t>(s)];
            if (slot.lightIndex < 0)
            {
                replaceSlot = s;
                break;
            }
            if (!isDesired(slot.lightIndex))
            {
                const float d = glm::distance(slot.pos, cameraPos);
                if (d > farthestDist)
                {
                    farthestDist = d;
                    replaceSlot = s;
                }
            }
        }

        if (replaceSlot < 0)
        {
            break;
        }

        PointShadowSlot &dst = m_pointShadowSlots[static_cast<std::size_t>(replaceSlot)];
        dst.lightIndex = targetIdx;
        dst.pos = glm::vec3(m_cpuLights[static_cast<std::size_t>(targetIdx)].positionRadius);
        dst.radius = std::max(0.5f, m_cpuLights[static_cast<std::size_t>(targetIdx)].positionRadius.w);
        dst.dirty = true;

        if (!allowMultiSwap)
        {
            break;
        }
    }

    const float fadeEnd = std::min(24.0f, fifthDist);
    const float fadeStart = fadeEnd * 0.72f;
    for (PointShadowSlot &slot : m_pointShadowSlots)
    {
        if (slot.lightIndex < 0)
        {
            slot.fade = 0.0f;
            slot.dirty = false;
            continue;
        }
        const float ds = glm::distance(slot.pos, cameraPos);
        const float t = std::clamp((fadeEnd - ds) / std::max(fadeEnd - fadeStart, 0.5f), 0.0f, 1.0f);
        slot.fade = t * t * (3.0f - 2.0f * t);
    }
}

void RenderResources::updateFrameUniforms(const RenderContext &ctx)
{
    m_frameUboSlot = (m_frameUboSlot + 1u) % kFrameUboSlots;
    if (m_frameUboFences[m_frameUboSlot] != nullptr)
    {
        glClientWaitSync(m_frameUboFences[m_frameUboSlot], GL_SYNC_FLUSH_COMMANDS_BIT, 1'000'000'000ULL);
        glDeleteSync(m_frameUboFences[m_frameUboSlot]);
        m_frameUboFences[m_frameUboSlot] = nullptr;
    }

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
    glm::vec3 lightDir = (glm::dot(ctx.moonDirection, ctx.moonDirection) > 1e-6f)
        ? glm::normalize(ctx.moonDirection)
        : glm::normalize(glm::vec3(0.45f, 0.85f, 0.35f));
    const glm::vec3 shadowUp = (glm::length(glm::vec2(lightDir.x, lightDir.z)) > 1e-3f)
        ? glm::vec3(0.0f, 1.0f, 0.0f)
        : glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), -lightDir, shadowUp);
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
    ubo.shadowParams = glm::vec4(
        1.0f / static_cast<float>(kShadowMapSize),
        kShadowHalfExtent,
        ctx.features.enableShadows ? 1.0f : 0.0f,
        ctx.features.enablePointShadows ? 1.0f : 0.0f);
    ubo.moonDir = glm::vec4(lightDir, 0.0f);

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

    const bool sceneChanged = (m_scene.generation() != m_lastHzbSceneGen);
    const bool cameraJumped = (m_hasPrevCameraPos && glm::distance(ctx.cameraPos, m_prevCameraPos) > 4.0f);
    if (sceneChanged || cameraJumped || !ctx.features.enableHzb)
    {
        m_hzbValid = false;
    }
    if (m_scene.generation() != m_lastPointShadowSceneGen)
    {
        invalidatePointShadows();
        m_lastPointShadowSceneGen = m_scene.generation();
    }
    if (cameraJumped)
    {
        m_pointShadowBurst = true;
    }
    m_lastHzbSceneGen = m_scene.generation();
    m_prevCameraPos = ctx.cameraPos;
    m_hasPrevCameraPos = true;

    if (ctx.features.enablePointShadows)
    {
        updatePointShadowSlots(ctx.cameraPos, m_pointShadowBurst);
        m_pointShadowBurst = false;
    }

    std::uint32_t featureMask = 0u;
    if (ctx.features.enableGi) { featureMask |= kFeatureGiBit; }
    if (ctx.features.enableSsao) { featureMask |= kFeatureSsaoBit; }
    if (ctx.features.enableFog) { featureMask |= kFeatureFogBit; }
    if (ctx.features.enableBloom) { featureMask |= kFeatureBloomBit; }
    if (ctx.features.enableSspr) { featureMask |= kFeatureSsrBit; }
    if (ctx.features.enableShadows) { featureMask |= kFeatureShadowsBit; }
    if (ctx.features.enablePointShadows) { featureMask |= kFeaturePointShadowsBit; }
    if (ctx.features.enableIndirectOnly) { featureMask |= kFeatureIndirectOnlyBit; }

    ubo.prevViewProj = m_hasPrevViewProj ? m_prevViewProj : vp;
    ubo.fogParams = glm::vec4(ctx.features.enableFog ? ctx.fogDensity : 0.0f, ctx.fogAnisotropy, ctx.fogHeightFalloff, ctx.fogFar);
    ubo.fogDims = glm::uvec4(kFogVolumeWidth, kFogVolumeHeight, kFogVolumeDepth, featureMask);
    ubo.hzbParams = ctx.features.enableHzb
        ? glm::uvec4(static_cast<std::uint32_t>(m_hzbSize.x), static_cast<std::uint32_t>(m_hzbSize.y), static_cast<std::uint32_t>(m_hzbMipCount), m_hzbValid ? 1u : 0u)
        : glm::uvec4(0u);
    for (int s = 0; s < kMaxPointShadowCasters; ++s)
    {
        const PointShadowSlot &slot = m_pointShadowSlots[static_cast<std::size_t>(s)];
        const bool slotActive = ctx.features.enablePointShadows && (slot.lightIndex >= 0);
        ubo.pointShadowMeta[static_cast<std::size_t>(s)] = glm::uvec4(
            slotActive ? static_cast<std::uint32_t>(slot.lightIndex) : 0xFFFFFFFFu,
            std::bit_cast<std::uint32_t>(kPointShadowNear),
            std::bit_cast<std::uint32_t>(slot.radius),
            std::bit_cast<std::uint32_t>(slot.fade));
    }
    m_prevViewProj = vp;
    m_hasPrevViewProj = true;

    std::memcpy(m_frameUboRing[m_frameUboSlot].mappedPtr(), &ubo, sizeof(FrameUniforms));
}

void RenderResources::markFrameSubmitted() noexcept
{
    if (m_frameUboFences[m_frameUboSlot] != nullptr)
    {
        glDeleteSync(m_frameUboFences[m_frameUboSlot]);
    }
    m_frameUboFences[m_frameUboSlot] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
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

StarterMaterialsCpuData RenderResources::generateStarterMaterialsCpu()
{
    constexpr int srcSize = 256;
    constexpr int dstSize = kMaterialArraySize;

    struct StarterTexel
    {
        glm::vec3 albedo{0.5f};
        float ao = 1.0f;
        float roughness = 0.8f;
        float metallic = 0.0f;
        float height = 0.5f;
    };

    StarterMaterialsCpuData result;
    result.layers.reserve(7);
    std::vector<StarterTexel> srcTexels(static_cast<std::size_t>(srcSize * srcSize));

    auto buildLayer = [&](int layer, float normalStrength, auto &&generator)
    {
        for (int y = 0; y < srcSize; ++y)
        {
            for (int x = 0; x < srcSize; ++x)
            {
                srcTexels[static_cast<std::size_t>(y * srcSize + x)] = generator(x, y);
            }
        }

        StarterMaterialsCpuData::Layer outLayer;
        outLayer.layerIndex = layer;
        const std::size_t byteCount = static_cast<std::size_t>(dstSize * dstSize * 4);
        outLayer.albedoPixels.resize(byteCount);
        outLayer.ormPixels.resize(byteCount);
        outLayer.normalPixels.resize(byteCount);

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
                outLayer.albedoPixels[idx + 0] = static_cast<std::uint8_t>(std::clamp(albedo.r, 0.0f, 1.0f) * 255.0f);
                outLayer.albedoPixels[idx + 1] = static_cast<std::uint8_t>(std::clamp(albedo.g, 0.0f, 1.0f) * 255.0f);
                outLayer.albedoPixels[idx + 2] = static_cast<std::uint8_t>(std::clamp(albedo.b, 0.0f, 1.0f) * 255.0f);
                outLayer.albedoPixels[idx + 3] = 255;

                outLayer.ormPixels[idx + 0] = static_cast<std::uint8_t>(std::clamp(ao, 0.0f, 1.0f) * 255.0f);
                outLayer.ormPixels[idx + 1] = static_cast<std::uint8_t>(std::clamp(roughness, 0.0f, 1.0f) * 255.0f);
                outLayer.ormPixels[idx + 2] = static_cast<std::uint8_t>(std::clamp(metallic, 0.0f, 1.0f) * 255.0f);
                outLayer.ormPixels[idx + 3] = 255;

                outLayer.normalPixels[idx + 0] = static_cast<std::uint8_t>(std::clamp(nrm.x * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
                outLayer.normalPixels[idx + 1] = static_cast<std::uint8_t>(std::clamp(nrm.y * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
                outLayer.normalPixels[idx + 2] = static_cast<std::uint8_t>(std::clamp(nrm.z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
                outLayer.normalPixels[idx + 3] = static_cast<std::uint8_t>(std::clamp(height, 0.0f, 1.0f) * 255.0f);
            }
        }
        result.layers.push_back(std::move(outLayer));
    };

    auto srgbToLin = [](glm::vec3 c) -> glm::vec3
    {
        return glm::pow(glm::max(c, glm::vec3(0.0f)), glm::vec3(2.2f));
    };

    // Layer 2: Burnished antique metallic gold / brass with hammered surface relief for SOR relics
    buildLayer(2, 2.4f, [&](int x, int y) -> StarterTexel
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
    buildLayer(3, 2.8f, [&](int x, int y) -> StarterTexel
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
    buildLayer(4, 1.2f, [&](int x, int y) -> StarterTexel
    {
        const float u = static_cast<float>(x) / 256.0f;
        const float v = static_cast<float>(y) / 256.0f;
        const float grain = 0.5f + 0.5f * std::sin(u * 50.265f + v * 18.849f) * std::cos(v * 50.265f);
        const glm::vec3 col = srgbToLin(glm::mix(glm::vec3(0.24f, 0.20f, 0.14f), glm::vec3(0.56f, 0.46f, 0.31f), u));
        return StarterTexel{.albedo = col, .roughness = glm::mix(0.45f, 0.90f, u), .height = 0.4f + 0.2f * grain};
    });
    buildLayer(5, 0.8f, [&](int x, int y) -> StarterTexel
    {
        const float u = static_cast<float>(x) / 256.0f;
        const float v = static_cast<float>(y) / 256.0f;
        const float caustic = 0.5f + 0.5f * std::sin(u * 18.849f + std::sin(v * 12.566f) * 1.6f) * std::cos(v * 18.849f);
        return StarterTexel{.albedo = srgbToLin(glm::mix(glm::vec3(0.05f, 0.24f, 0.28f), glm::vec3(0.11f, 0.40f, 0.42f), caustic * 0.55f)), .roughness = 0.12f, .height = 0.5f + 0.1f * caustic};
    });
    buildLayer(6, 2.0f, [&](int x, int y) -> StarterTexel
    {
        const float u = static_cast<float>(x) / 256.0f;
        const float v = static_cast<float>(y) / 256.0f;
        const float ringBand = 0.5f + 0.5f * std::sin(v * 37.699f);
        const float bark = 0.5f + 0.5f * std::sin(u * 75.398f + v * 12.566f);
        return StarterTexel{.albedo = srgbToLin(glm::mix(glm::vec3(0.20f, 0.14f, 0.09f), glm::vec3(0.34f, 0.25f, 0.17f), ringBand)), .roughness = 0.86f, .height = 0.4f + 0.25f * bark};
    });
    buildLayer(7, 2.4f, [&](int x, int y) -> StarterTexel
    {
        const float u = static_cast<float>(x) / 256.0f;
        const float v = static_cast<float>(y) / 256.0f;
        const float woodyToLeaf = std::clamp((u - 0.10f) * 16.0f, 0.0f, 1.0f);
        const float leafSpan = std::abs(u - 0.56f) / 0.40f;
        const float midrib = std::exp(-leafSpan * leafSpan * 28.0f);
        const float lateralVein = 0.5f + 0.5f * std::sin(v * 44.0f - leafSpan * 10.5f);
        const float tipBlend = std::clamp(v, 0.0f, 1.0f);

        const glm::vec3 stemCol(0.11f, 0.21f, 0.08f);
        const glm::vec3 baseLeaf(0.10f, 0.24f, 0.07f);
        const glm::vec3 tipLeaf(0.24f, 0.45f, 0.14f);
        const glm::vec3 veinTint(0.34f, 0.55f, 0.20f);

        glm::vec3 leafCol = glm::mix(baseLeaf, tipLeaf, tipBlend);
        leafCol *= 0.90f + 0.14f * lateralVein;
        leafCol = glm::mix(leafCol, veinTint, std::clamp(midrib * 0.70f + lateralVein * 0.10f, 0.0f, 0.75f));

        const glm::vec3 col = glm::mix(stemCol, leafCol, woodyToLeaf);
        const float height = 0.38f + woodyToLeaf * (0.28f * midrib + 0.16f * lateralVein);
        return StarterTexel{
            .albedo = col,
            .ao = glm::mix(0.66f, 0.96f, tipBlend),
            .roughness = glm::mix(0.76f, 0.62f + 0.14f * (1.0f - lateralVein), woodyToLeaf),
            .metallic = 0.0f,
            .height = height,
        };
    });

    // Layer 9: Multi-band brazier torch mapped by normalized lathe height v (wood handle, iron collars, forged brazier cup, sooty rim)
    buildLayer(9, 2.2f, [&](int x, int y) -> StarterTexel
    {
        const float u = static_cast<float>(x) / 256.0f;
        const float v = static_cast<float>(y) / 256.0f;
        const float twoPi = 6.283185307f;

        // Strictly U-periodic grain & forged hammer relief across atan2 seam
        const float woodFiber = 0.5f + 0.35f * std::sin(u * twoPi * 10.0f + std::sin(v * 24.0f) * 1.1f)
                                     + 0.15f * std::cos(u * twoPi * 22.0f - v * 9.0f);
        const float woodRing = 0.5f + 0.5f * std::sin(v * 42.0f + woodFiber * 2.4f);
        const glm::vec3 woodDark = srgbToLin(glm::vec3(0.24f, 0.11f, 0.08f));
        const glm::vec3 woodLight = srgbToLin(glm::vec3(0.40f, 0.20f, 0.15f));
        const glm::vec3 woodCol = glm::mix(woodDark, woodLight, std::clamp(woodFiber * 0.68f + woodRing * 0.32f, 0.0f, 1.0f));

        const float hammer = 0.5f + 0.5f * std::sin(u * twoPi * 8.0f + std::sin(v * 38.0f) * 1.3f) * std::cos(v * 44.0f);
        const float brush = 0.5f + 0.5f * std::sin(u * twoPi * 16.0f + v * 14.0f);
        const glm::vec3 collarCol = srgbToLin(glm::mix(glm::vec3(0.46f, 0.48f, 0.53f), glm::vec3(0.64f, 0.67f, 0.72f), hammer * 0.65f + brush * 0.35f));
        const glm::vec3 cupCol = srgbToLin(glm::mix(glm::vec3(0.26f, 0.27f, 0.31f), glm::vec3(0.39f, 0.40f, 0.45f), hammer * 0.70f + brush * 0.30f));

        auto bandMask = [&](float a, float b, float feather) -> float
        {
            const float lo = std::clamp((v - (a - feather)) / std::max(2.0f * feather, 1e-4f), 0.0f, 1.0f);
            const float hi = std::clamp(((b + feather) - v) / std::max(2.0f * feather, 1e-4f), 0.0f, 1.0f);
            const float sLo = lo * lo * (3.0f - 2.0f * lo);
            const float sHi = hi * hi * (3.0f - 2.0f * hi);
            return sLo * sHi;
        };
        auto seamGroove = [&](float edge) -> float
        {
            const float d = std::abs(v - edge);
            return std::exp(-(d * d) / (0.0085f * 0.0085f));
        };

        const float pommelCap = bandMask(-0.02f, 0.035f, 0.008f);
        const float lowerCollar = bandMask(0.16f, 0.25f, 0.009f);
        const float upperCollar = bandMask(0.54f, 0.635f, 0.010f);
        const float collarMask = std::max({pommelCap, lowerCollar, upperCollar});
        const float cupMask = std::clamp((v - 0.630f) / 0.020f, 0.0f, 1.0f);
        const float sootMask = std::clamp((v - 0.945f) / 0.045f, 0.0f, 1.0f);

        const float groove = std::max({seamGroove(0.035f), seamGroove(0.16f), seamGroove(0.25f), seamGroove(0.54f), seamGroove(0.635f)});

        glm::vec3 albedo = woodCol;
        float roughness = 0.72f + (1.0f - woodFiber) * 0.12f;
        float metallic = 0.0f;
        float height = 0.36f + woodFiber * 0.14f;
        float ao = 0.84f + 0.16f * woodFiber;

        // Forged iron cup on upper half
        albedo = glm::mix(albedo, cupCol, cupMask);
        roughness = glm::mix(roughness, 0.36f + (1.0f - hammer) * 0.14f, cupMask);
        metallic = glm::mix(metallic, 0.94f, cupMask);
        height = glm::mix(height, 0.54f + hammer * 0.16f, cupMask);
        ao = glm::mix(ao, 0.78f + 0.22f * hammer, cupMask);

        // Brighter pewter-steel raised collars on shaft & brazier neck
        albedo = glm::mix(albedo, collarCol, collarMask);
        roughness = glm::mix(roughness, 0.30f + (1.0f - brush) * 0.12f, collarMask);
        metallic = glm::mix(metallic, 1.0f, collarMask);
        height = glm::mix(height, 0.78f + hammer * 0.12f, collarMask);
        ao = glm::mix(ao, 0.86f + 0.14f * brush, collarMask);

        // Sooty charred ash rim around top brazier lip
        const glm::vec3 sootCol = srgbToLin(glm::vec3(0.06f, 0.055f, 0.055f));
        albedo = glm::mix(albedo, sootCol, sootMask * 0.88f);
        roughness = glm::mix(roughness, 0.92f, sootMask);
        metallic = glm::mix(metallic, 0.12f, sootMask);
        ao *= 1.0f - 0.38f * sootMask;

        // Recessed mechanical seam crevice where metal meets wood or cup
        ao *= 1.0f - 0.65f * groove;
        height = std::max(0.05f, height - 0.28f * groove);

        return StarterTexel{
            .albedo = albedo,
            .ao = ao,
            .roughness = roughness,
            .metallic = metallic,
            .height = height,
        };
    });

    return result;
}

void RenderResources::uploadStarterMaterialArrays(const StarterMaterialsCpuData &cpuData)
{
    constexpr int dstSize = kMaterialArraySize;
    constexpr int layers = kMaterialArrayLayers;

    if (m_albedoArrayTex == 0)
    {
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
    }

    for (const StarterMaterialsCpuData::Layer &layer : cpuData.layers)
    {
        glTextureSubImage3D(m_albedoArrayTex, 0, 0, 0, layer.layerIndex, dstSize, dstSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, layer.albedoPixels.data());
        glTextureSubImage3D(m_ormArrayTex, 0, 0, 0, layer.layerIndex, dstSize, dstSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, layer.ormPixels.data());
        glTextureSubImage3D(m_normalArrayTex, 0, 0, 0, layer.layerIndex, dstSize, dstSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, layer.normalPixels.data());
    }

    glGenerateTextureMipmap(m_albedoArrayTex);
    glGenerateTextureMipmap(m_ormArrayTex);
    glGenerateTextureMipmap(m_normalArrayTex);
}

void RenderResources::createStarterMaterialArrays()
{
    uploadStarterMaterialArrays(generateStarterMaterialsCpu());
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
