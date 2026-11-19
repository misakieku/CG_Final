#include "game/render/passes/BloomPass.hpp"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <algorithm>

namespace game::render
{

BloomPass::BloomPass(const std::filesystem::path &shaderDir)
{
    reloadShaders(shaderDir);
}

void BloomPass::reloadShaders(const std::filesystem::path &shaderDir)
{
    gl::Shader pre = gl::Shader::compileFiles(shaderDir / "common/fullscreen.vert", shaderDir / "passes/bloom_prefilter.frag", shaderDir);
    gl::Shader down = gl::Shader::compileFiles(shaderDir / "common/fullscreen.vert", shaderDir / "passes/bloom_downsample.frag", shaderDir);
    gl::Shader up = gl::Shader::compileFiles(shaderDir / "common/fullscreen.vert", shaderDir / "passes/bloom_upsample.frag", shaderDir);
    m_prefilterShader = std::move(pre);
    m_downsampleShader = std::move(down);
    m_upsampleShader = std::move(up);
}

void BloomPass::execute(const RenderContext &ctx, RenderResources &res)
{
    if (!ctx.features.enableBloom)
    {
        return;
    }
    const glm::ivec2 bloomSize = res.bloomSize();
    const int mipCount = res.bloomMipCount();
    const GLuint bloomTex = res.bloomTexId();
    if (bloomTex == 0 || mipCount <= 0 || bloomSize.x <= 0 || bloomSize.y <= 0)
    {
        return;
    }

    res.bloomFbo().bind();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    // Fog-aware Karis prefilter from full-res HDR into half-res Mip 0
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex, 0);
    glViewport(0, 0, bloomSize.x, bloomSize.y);
    m_prefilterShader.use();
    m_prefilterShader.setVec2("uSrcTexelSize", glm::vec2(1.0f / static_cast<float>(std::max(1, ctx.viewportWidth)), 1.0f / static_cast<float>(std::max(1, ctx.viewportHeight))));
    m_prefilterShader.setFloat("uThreshold", 1.05f);
    m_prefilterShader.setFloat("uKnee", 0.55f);
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    glBindTextureUnit(0, res.hdrTex().id());
    glBindTextureUnit(1, res.depthTex().id());
    glBindTextureUnit(2, res.sampledFogAccumTexId(ctx.features.enableFog));
    res.drawFullscreenTriangle();

    // 13-tap progressive downsample across the bloom mip pyramid
    m_downsampleShader.use();
    glBindTextureUnit(0, bloomTex);
    for (int dstMip = 1; dstMip < mipCount; ++dstMip)
    {
        const int srcMip = dstMip - 1;
        const glm::ivec2 srcSize = glm::max(glm::ivec2(1), bloomSize >> srcMip);
        const glm::ivec2 dstSize = glm::max(glm::ivec2(1), bloomSize >> dstMip);
        glTextureParameteri(bloomTex, GL_TEXTURE_BASE_LEVEL, srcMip);
        glTextureParameteri(bloomTex, GL_TEXTURE_MAX_LEVEL, srcMip);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex, dstMip);
        glViewport(0, 0, dstSize.x, dstSize.y);
        m_downsampleShader.setVec2("uSrcTexelSize", glm::vec2(1.0f / static_cast<float>(srcSize.x), 1.0f / static_cast<float>(srcSize.y)));
        res.drawFullscreenTriangle();
    }

    // 9-tap tent upsample accumulating coarser mips back onto finer mips
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    m_upsampleShader.use();
    m_upsampleShader.setFloat("uFilterRadius", 1.15f);
    for (int dstMip = mipCount - 2; dstMip >= 0; --dstMip)
    {
        const int srcMip = dstMip + 1;
        const glm::ivec2 srcSize = glm::max(glm::ivec2(1), bloomSize >> srcMip);
        const glm::ivec2 dstSize = glm::max(glm::ivec2(1), bloomSize >> dstMip);
        glTextureParameteri(bloomTex, GL_TEXTURE_BASE_LEVEL, srcMip);
        glTextureParameteri(bloomTex, GL_TEXTURE_MAX_LEVEL, srcMip);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex, dstMip);
        glViewport(0, 0, dstSize.x, dstSize.y);
        m_upsampleShader.setVec2("uSrcTexelSize", glm::vec2(1.0f / static_cast<float>(srcSize.x), 1.0f / static_cast<float>(srcSize.y)));
        res.drawFullscreenTriangle();
    }

    glDisable(GL_BLEND);
    glTextureParameteri(bloomTex, GL_TEXTURE_BASE_LEVEL, 0);
    glTextureParameteri(bloomTex, GL_TEXTURE_MAX_LEVEL, mipCount - 1);
}

} // namespace game::render
