#include "game/render/passes/FxaaPass.hpp"

#include <glad/gl.h>

#include <utility>

namespace game::render
{

FxaaPass::FxaaPass(const std::filesystem::path &shaderDir)
{
    reloadShaders(shaderDir);
}

void FxaaPass::reloadShaders(const std::filesystem::path &shaderDir)
{
    gl::Shader compiled = gl::Shader::compileFiles(shaderDir / "common/fullscreen.vert", shaderDir / "passes/fxaa.frag", shaderDir);
    m_fxaaShader = std::move(compiled);
}

void FxaaPass::execute(const RenderContext &ctx, RenderResources &res)
{
    const int outW = (ctx.presentWidth > 0) ? ctx.presentWidth : ctx.viewportWidth;
    const int outH = (ctx.presentHeight > 0) ? ctx.presentHeight : ctx.viewportHeight;

    if (!ctx.features.enableFxaa)
    {
        const GLenum filter = (outW == ctx.viewportWidth && outH == ctx.viewportHeight) ? GL_NEAREST : GL_LINEAR;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, res.ldrFbo().id());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(
            0, 0, ctx.viewportWidth, ctx.viewportHeight,
            0, 0, outW, outH,
            GL_COLOR_BUFFER_BIT,
            filter);
        gl::Framebuffer::bindDefault();
        return;
    }

    gl::Framebuffer::bindDefault();
    glViewport(0, 0, outW, outH);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    m_fxaaShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    glBindTextureUnit(0, res.ldrTex().id());

    res.drawFullscreenTriangle();
}

} // namespace game::render
