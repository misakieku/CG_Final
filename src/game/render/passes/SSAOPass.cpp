#include "game/render/passes/SSAOPass.hpp"

#include <glad/gl.h>

#include <utility>

namespace game::render
{

SSAOPass::SSAOPass(const std::filesystem::path &shaderDir)
{
    reloadShaders(shaderDir);
}

void SSAOPass::reloadShaders(const std::filesystem::path &shaderDir)
{
    gl::Shader raw = gl::Shader::compileFiles(shaderDir / "common/fullscreen.vert", shaderDir / "passes/ssao.frag", shaderDir);
    gl::Shader blur = gl::Shader::compileFiles(shaderDir / "common/fullscreen.vert", shaderDir / "passes/ssao_blur.frag", shaderDir);
    m_ssaoShader = std::move(raw);
    m_blurShader = std::move(blur);
}

void SSAOPass::execute(const RenderContext &ctx, RenderResources &res)
{
    if (!ctx.features.enableSsao || res.aoWidth() <= 0 || res.aoHeight() <= 0)
    {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    // Raw half-resolution oriented-hemisphere occlusion
    res.aoRawFbo().bind();
    glViewport(0, 0, res.aoWidth(), res.aoHeight());
    m_ssaoShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    glBindTextureUnit(0, res.depthTex().id());
    glBindTextureUnit(1, res.gbuffer1().id());
    res.drawFullscreenTriangle();

    // Depth-aware 3x3 cross-bilateral filter removing stochastic noise
    res.aoFbo().bind();
    glViewport(0, 0, res.aoWidth(), res.aoHeight());
    m_blurShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    glBindTextureUnit(0, res.aoRawTex().id());
    glBindTextureUnit(1, res.depthTex().id());
    res.drawFullscreenTriangle();
}

} // namespace game::render
