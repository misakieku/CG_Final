#include "game/render/passes/GBufferPass.hpp"

#include <glad/gl.h>

namespace game::render
{

GBufferPass::GBufferPass(const std::filesystem::path &shaderDir)
{
    reloadShaders(shaderDir);
}

void GBufferPass::reloadShaders(const std::filesystem::path &shaderDir)
{
    gl::Shader compiled = gl::Shader::compileFiles(shaderDir / "passes/gbuffer.vert", shaderDir / "passes/gbuffer.frag", shaderDir);
    gl::Shader grassCompiled = gl::Shader::compileFiles(shaderDir / "passes/grass_gbuffer.vert", shaderDir / "passes/grass_gbuffer.frag", shaderDir);
    m_gbufferShader = std::move(compiled);
    m_grassGbufferShader = std::move(grassCompiled);
}

void GBufferPass::execute(const RenderContext &ctx, RenderResources &res)
{
    res.gbufferFbo().bind();
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);

    // Compare strictly against the DepthPrePass buffer (read-only Z, Early-Z guaranteed)
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_EQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const MegaScene &scene = res.scene();
    if (scene.meshCount() > 0 && scene.objectCount() > 0)
    {
        m_gbufferShader.use();
        res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
        scene.bindSceneBuffers(0, 1, 2, 3);
        glBindTextureUnit(0, res.albedoArrayTexId());
        glBindTextureUnit(1, res.ormArrayTexId());
        glBindTextureUnit(2, res.normalArrayTexId());

        scene.bindMegaVao();
        scene.bindDrawIndirectBuffer();
        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(scene.meshCount()), 0);
        glBindVertexArray(0);
    }

    if (res.grass().instanceCount() > 0)
    {
        glDisable(GL_CULL_FACE);
        m_grassGbufferShader.use();
        res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
        res.grass().drawIndirect(0, 2);
        glEnable(GL_CULL_FACE);
    }

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_GREATER);
}

} // namespace game::render
