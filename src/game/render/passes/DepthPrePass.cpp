#include "game/render/passes/DepthPrePass.hpp"

#include <glad/gl.h>

namespace game::render
{

DepthPrePass::DepthPrePass(const std::filesystem::path &shaderDir)
{
    reloadShaders(shaderDir);
}

void DepthPrePass::reloadShaders(const std::filesystem::path &shaderDir)
{
    gl::Shader compiled = gl::Shader::compileFiles(shaderDir / "passes/depth_prepass.vert", shaderDir / "passes/depth_prepass.frag", shaderDir);
    m_depthShader = std::move(compiled);
}

void DepthPrePass::execute(const RenderContext &ctx, RenderResources &res)
{
    res.gbufferFbo().bind();
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);

    // Reversed-Z depth-only pre-pass: clear depth to 0.0 (far), pass GREATER
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_GREATER);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glClearDepth(0.0);
    glClear(GL_DEPTH_BUFFER_BIT);

    const MegaScene &scene = res.scene();
    if (scene.meshCount() > 0 && scene.objectCount() > 0)
    {
        m_depthShader.use();
        res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
        scene.bindSceneBuffers(0, 1, 2, 3);

        scene.bindMegaVao();
        scene.bindDrawIndirectBuffer();
        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(scene.meshCount()), 0);
        glBindVertexArray(0);
    }

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

} // namespace game::render
