#include "game/render/passes/HzbPass.hpp"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <algorithm>

namespace game::render
{

HzbPass::HzbPass(const std::filesystem::path &shaderDir, bool markHistoryValid)
    : m_markHistoryValid(markHistoryValid)
{
    reloadShaders(shaderDir);
}

void HzbPass::reloadShaders(const std::filesystem::path &shaderDir)
{
    m_buildShader = gl::Shader::compileComputeFile(shaderDir / "passes/build_hzb.comp", shaderDir);
}

void HzbPass::execute(const RenderContext &ctx, RenderResources &res)
{
    if (!ctx.features.enableHzb)
    {
        return;
    }
    const glm::ivec2 hzbSize = res.hzbSize();
    const int mipCount = res.hzbMipCount();
    if (hzbSize.x <= 0 || hzbSize.y <= 0 || mipCount <= 0)
    {
        return;
    }

    const GLuint hzbTex = res.currHzbTexId();
    if (hzbTex == 0)
    {
        return;
    }

    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    m_buildShader.use();

    for (int baseMip = 0; baseMip < mipCount; baseMip += 4)
    {
        const bool firstBatch = (baseMip == 0);
        const int batchMips = std::min(4, mipCount - baseMip);

        const glm::ivec2 srcSize = firstBatch
            ? glm::ivec2(std::max(1, ctx.viewportWidth), std::max(1, ctx.viewportHeight))
            : glm::max(glm::ivec2(1), hzbSize >> (baseMip - 1));

        auto mipSizeAt = [&](int offset) -> glm::ivec2
        {
            const int level = std::min(baseMip + offset, mipCount - 1);
            return glm::max(glm::ivec2(1), hzbSize >> level);
        };

        const glm::ivec2 dst0 = mipSizeAt(0);
        const glm::ivec2 dst1 = mipSizeAt(1);
        const glm::ivec2 dst2 = mipSizeAt(2);
        const glm::ivec2 dst3 = mipSizeAt(3);

        m_buildShader.setInt("uFirstBatch", firstBatch ? 1 : 0);
        m_buildShader.setInt("uMinDstCount", batchMips);
        m_buildShader.setIVec2("uSrcSize", srcSize);
        m_buildShader.setIVec4("uDstSize01", glm::ivec4(dst0.x, dst0.y, dst1.x, dst1.y));
        m_buildShader.setIVec4("uDstSize23", glm::ivec4(dst2.x, dst2.y, dst3.x, dst3.y));

        if (firstBatch)
        {
            glBindTextureUnit(0, res.depthTex().id());
            glBindImageTexture(0, hzbTex, mipCount - 1, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
        }
        else
        {
            glBindImageTexture(0, hzbTex, baseMip - 1, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
        }

        for (int i = 0; i < 4; ++i)
        {
            const int dstMip = baseMip + std::min(i, batchMips - 1);
            glBindImageTexture(static_cast<GLuint>(1 + i), hzbTex, dstMip, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
        }

        const GLuint groupsX = static_cast<GLuint>((dst0.x + 7) / 8);
        const GLuint groupsY = static_cast<GLuint>((dst0.y + 7) / 8);
        glDispatchCompute(groupsX, groupsY, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }

    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    if (m_markHistoryValid)
    {
        res.markHzbValid();
    }
}

} // namespace game::render
