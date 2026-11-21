#include "game/render/passes/GIPass.hpp"

#include <glad/gl.h>

#include <array>
#include <cstdint>

namespace game::render
{

GIPass::GIPass(const std::filesystem::path &shaderDir)
{
    reloadShaders(shaderDir);
}

void GIPass::reloadShaders(const std::filesystem::path &shaderDir)
{
    m_markShader = gl::Shader::compileComputeFile(shaderDir / "passes/gi_mark.comp", shaderDir);
    m_compactShader = gl::Shader::compileComputeFile(shaderDir / "passes/gi_compact.comp", shaderDir);
    m_traceShader = gl::Shader::compileComputeFile(shaderDir / "passes/gi_trace.comp", shaderDir);
    m_blendShader = gl::Shader::compileComputeFile(shaderDir / "passes/gi_blend.comp", shaderDir);
    m_screenShader = gl::Shader::compileFiles(shaderDir / "common/fullscreen.vert", shaderDir / "passes/gi_screen.frag", shaderDir);
}

void GIPass::execute(const RenderContext &ctx, RenderResources &res)
{
    if (!ctx.features.enableGi || !res.hasGiGrid())
    {
        return;
    }

    const glm::uvec3 dims = res.giProbeDims();
    const std::uint32_t totalProbes = dims.x * dims.y * dims.z;
    if (totalProbes == 0)
    {
        return;
    }

    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    res.giProbeMetaSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 7);
    res.giActiveFlagSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 8);
    res.giActiveListSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 9);
    res.giDispatchCmdSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 10);
    res.giStagingShSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 11);
    res.giOctCacheSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 12);

    // Clear per-frame active flags & reset indirect dispatch commands: trace at +0, blend at +16
    res.giActiveFlagSSBO().clearUint32(0u);
    constexpr std::array<std::uint32_t, 8> kInitDispatch{0u, 1u, 1u, 0u, 0u, 1u, 1u, 0u};
    res.giDispatchCmdSSBO().upload(0, sizeof(kInitDispatch), kInitDispatch.data());
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

    // Mark probes touched by visible screen-space depth pixels
    m_markShader.use();
    glBindTextureUnit(0, res.depthTex().id());
    glBindTextureUnit(1, res.gbuffer1().id());
    glBindTextureUnit(2, res.mazeGridTexId());
    const GLuint halfW = static_cast<GLuint>((ctx.viewportWidth + 1) / 2);
    const GLuint halfH = static_cast<GLuint>((ctx.viewportHeight + 1) / 2);
    glDispatchCompute((halfW + 7u) / 8u, (halfH + 7u) / 8u, 1u);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // Compact active probe IDs & increment DispatchIndirect X count
    m_compactShader.use();
    glDispatchCompute((totalProbes + 63u) / 64u, 1u, 1u);
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

    // Trace 64 Grid-DDA rays per active visible probe (reads previous frame 3D SH for temporal multi-bounce)
    m_traceShader.use();
    res.lightsSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 4);
    glBindTextureUnit(0, res.giShTexId(0));
    glBindTextureUnit(1, res.giShTexId(1));
    glBindTextureUnit(2, res.giShTexId(2));
    glBindTextureUnit(3, res.mazeGridTexId());
    glBindTextureUnit(4, res.sampledShadowDepthTexId(ctx.features.enableShadows));
    glBindTextureUnit(5, res.albedoArrayTexId());

    res.giDispatchCmdSSBO().bind(GL_DISPATCH_INDIRECT_BUFFER);
    glDispatchComputeIndirect(0);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // Blend staged L1 SH coefficients into the persistent 3D SH textures (64 threads/group)
    m_blendShader.use();
    glBindImageTexture(0, res.giShTexId(0), 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
    glBindImageTexture(1, res.giShTexId(1), 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
    glBindImageTexture(2, res.giShTexId(2), 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
    glDispatchComputeIndirect(16);
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

    if (res.aoWidth() > 0 && res.aoHeight() > 0)
    {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);

        res.giScreenFbo().bind();
        glViewport(0, 0, res.aoWidth(), res.aoHeight());
        m_screenShader.use();
        res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
        glBindTextureUnit(0, res.depthTex().id());
        glBindTextureUnit(1, res.gbuffer1().id());
        glBindTextureUnit(2, res.giShTexId(0));
        glBindTextureUnit(3, res.giShTexId(1));
        glBindTextureUnit(4, res.giShTexId(2));
        glBindTextureUnit(5, res.mazeGridTexId());
        res.drawFullscreenTriangle();
    }
}

} // namespace game::render
