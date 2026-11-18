#include "game/render/passes/CompositePass.hpp"

#include <glad/gl.h>

namespace game::render
{

CompositePass::CompositePass(const std::filesystem::path &shaderDir)
{
    reloadShaders(shaderDir);
}

void CompositePass::reloadShaders(const std::filesystem::path &shaderDir)
{
    gl::Shader compiled = gl::Shader::compileFiles(shaderDir / "common/fullscreen.vert", shaderDir / "passes/composite.frag", shaderDir);
    m_compositeShader = std::move(compiled);
}

void CompositePass::execute(const RenderContext &ctx, RenderResources &res)
{
    res.hdrFbo().bind();
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    m_compositeShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    res.lightsSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 4);
    res.clusterCountsSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 5);
    res.clusterIndicesSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 6);

    glBindTextureUnit(0, res.gbuffer0().id());
    glBindTextureUnit(1, res.gbuffer1().id());
    glBindTextureUnit(2, res.depthTex().id());
    glBindTextureUnit(3, res.shadowDepthTex().id());

    res.drawFullscreenTriangle();
}

} // namespace game::render
