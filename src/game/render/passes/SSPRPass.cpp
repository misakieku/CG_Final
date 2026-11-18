#include "game/render/passes/SSPRPass.hpp"

#include <glad/gl.h>

#include <utility>

namespace game::render
{

SSPRPass::SSPRPass(const std::filesystem::path &shaderDir)
{
    reloadShaders(shaderDir);
}

void SSPRPass::reloadShaders(const std::filesystem::path &shaderDir)
{
    gl::Shader proj = gl::Shader::compileComputeFile(shaderDir / "passes/sspr_project.comp", shaderDir);
    gl::Shader resolve = gl::Shader::compileComputeFile(shaderDir / "passes/sspr_resolve.comp", shaderDir);
    gl::Shader apply = gl::Shader::compileComputeFile(shaderDir / "passes/sspr_apply.comp", shaderDir);
    m_projectShader = std::move(proj);
    m_resolveShader = std::move(resolve);
    m_applyShader = std::move(apply);
}

void SSPRPass::execute(const RenderContext &ctx, RenderResources &res)
{
    if (ctx.whiteboxMode || ctx.viewportWidth <= 0 || ctx.viewportHeight <= 0)
    {
        return;
    }

    const GLuint groupsX = (static_cast<GLuint>(ctx.viewportWidth) + 7u) / 8u;
    const GLuint groupsY = (static_cast<GLuint>(ctx.viewportHeight) + 7u) / 8u;

    const GLuint clearHash = 0xFFFFFFFFu;
    glClearTexImage(res.ssprHashTex().id(), 0, GL_RED_INTEGER, GL_UNSIGNED_INT, &clearHash);
    glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // Mirror geometry above y = 0 back into screen space using atomicMin depth priority
    m_projectShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    glBindTextureUnit(0, res.depthTex().id());
    glBindImageTexture(0, res.ssprHashTex().id(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
    glDispatchCompute(groupsX, groupsY, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // Resolve hit source HDR color + 3x3 hole fill into RGBA16F reflection texture
    m_resolveShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    glBindImageTexture(0, res.ssprHashTex().id(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
    glBindImageTexture(1, res.ssprReflectTex().id(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glBindTextureUnit(0, res.hdrTex().id());
    glBindTextureUnit(1, res.gbuffer1().id());
    glBindTextureUnit(2, res.depthTex().id());
    glDispatchCompute(groupsX, groupsY, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // Blend rippled SSPR + sky/GI fallback onto standing puddles in m_hdrTex
    m_applyShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    glBindImageTexture(0, res.hdrTex().id(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
    glBindTextureUnit(0, res.ssprReflectTex().id());
    glBindTextureUnit(1, res.gbuffer1().id());
    glBindTextureUnit(2, res.depthTex().id());
    if (res.hasGiGrid())
    {
        glBindTextureUnit(3, res.giShTexId(0));
        glBindTextureUnit(4, res.giShTexId(1));
        glBindTextureUnit(5, res.giShTexId(2));
        glBindTextureUnit(6, res.mazeGridTexId());
    }
    glDispatchCompute(groupsX, groupsY, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}

} // namespace game::render
