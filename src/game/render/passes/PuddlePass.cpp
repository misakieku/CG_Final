#include "game/render/passes/PuddlePass.hpp"

#include <glad/gl.h>

namespace game::render
{

PuddlePass::PuddlePass(const std::filesystem::path &shaderDir)
{
    reloadShaders(shaderDir);
}

void PuddlePass::reloadShaders(const std::filesystem::path &shaderDir)
{
    gl::Shader compiled = gl::Shader::compileFiles(shaderDir / "common/fullscreen.vert", shaderDir / "passes/puddle.frag", shaderDir);
    m_puddleShader = std::move(compiled);
}

void PuddlePass::execute(const RenderContext &ctx, RenderResources &res)
{
    res.puddleFbo().bind();
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    // Core OpenGL 4.5 single-pass read-modify-write barrier on GBuffer0/GBuffer1
    glTextureBarrier();

    m_puddleShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    glBindTextureUnit(0, res.gbuffer0().id());
    glBindTextureUnit(1, res.gbuffer1().id());
    glBindTextureUnit(2, res.depthTex().id());

    res.drawFullscreenTriangle();
    glTextureBarrier();
}

} // namespace game::render
