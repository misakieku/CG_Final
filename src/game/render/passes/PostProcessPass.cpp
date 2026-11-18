#include "game/render/passes/PostProcessPass.hpp"

#include <glad/gl.h>

namespace game::render
{

PostProcessPass::PostProcessPass(const std::filesystem::path &shaderDir)
{
    reloadShaders(shaderDir);
}

void PostProcessPass::reloadShaders(const std::filesystem::path &shaderDir)
{
    gl::Shader compiled = gl::Shader::compileFiles(shaderDir / "common/fullscreen.vert", shaderDir / "passes/postprocess.frag", shaderDir);
    m_postShader = std::move(compiled);
}

void PostProcessPass::execute(const RenderContext &ctx, RenderResources &res)
{
    gl::Framebuffer::bindDefault();
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    m_postShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    glBindTextureUnit(0, res.hdrTex().id());
    glBindTextureUnit(1, res.depthTex().id());
    glBindTextureUnit(2, res.fogAccumTexId());

    res.drawFullscreenTriangle();
}

} // namespace game::render
