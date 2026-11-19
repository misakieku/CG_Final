#include "game/render/passes/FlamePass.hpp"

#include <glad/gl.h>

namespace game::render
{

FlamePass::FlamePass(const std::filesystem::path &shaderDir)
{
    reloadShaders(shaderDir);
}

void FlamePass::reloadShaders(const std::filesystem::path &shaderDir)
{
    m_flameShader = gl::Shader::compileFiles(shaderDir / "passes/flame.vert", shaderDir / "passes/flame.frag", shaderDir);
}

void FlamePass::execute(const RenderContext &ctx, RenderResources &res)
{
    if (!ctx.features.enableFlames || res.flameCount() == 0)
    {
        return;
    }

    res.hdrDepthFbo().bind();
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_GREATER);
    glDisable(GL_CULL_FACE);

    // Premultiplied alpha: fire core blends cleanly over bright stone while alpha=0 halo stays purely additive
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    m_flameShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    res.flamesSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 0);

    glBindVertexArray(res.emptyVao());
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(res.flameCount()));
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
}

} // namespace game::render
