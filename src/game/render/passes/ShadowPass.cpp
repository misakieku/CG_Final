#include "game/render/passes/ShadowPass.hpp"

#include <glad/gl.h>

namespace game::render
{

ShadowPass::ShadowPass(const std::filesystem::path &shaderDir)
{
    reloadShaders(shaderDir);
}

void ShadowPass::reloadShaders(const std::filesystem::path &shaderDir)
{
    gl::Shader compiled = gl::Shader::compileFiles(shaderDir / "passes/shadow.vert", shaderDir / "passes/shadow.frag", shaderDir);
    m_shadowShader = std::move(compiled);
}

void ShadowPass::execute(const RenderContext &ctx, RenderResources &res)
{
    (void)ctx;
    res.shadowFbo().bind();
    glViewport(0, 0, RenderResources::kShadowMapSize, RenderResources::kShadowMapSize);

    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.4f, 2.5f);

    glClearDepth(1.0);
    glClear(GL_DEPTH_BUFFER_BIT);

    const MegaScene &scene = res.scene();
    if (scene.meshCount() > 0 && scene.objectCount() > 0)
    {
        m_shadowShader.use();
        res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
        scene.bindSceneBuffers(0, 1, 2, 3);

        scene.bindMegaVao();
        scene.bindDrawIndirectBuffer();
        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(scene.meshCount()), 0);
        glBindVertexArray(0);
    }

    glDisable(GL_POLYGON_OFFSET_FILL);
    glClearDepth(0.0);
    glDepthFunc(GL_GREATER);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

} // namespace game::render
