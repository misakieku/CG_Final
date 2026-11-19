#include "game/render/passes/DepthPrePass.hpp"

#include <glad/gl.h>

namespace game::render
{

DepthPrePass::DepthPrePass(const std::filesystem::path &shaderDir, DepthStage stage)
    : m_stage(stage)
{
    reloadShaders(shaderDir);
}

void DepthPrePass::reloadShaders(const std::filesystem::path &shaderDir)
{
    if (m_stage == DepthStage::Grass)
    {
        m_grassDepthShader = gl::Shader::compileFiles(shaderDir / "passes/grass_depth.vert", shaderDir / "passes/depth_prepass.frag", shaderDir);
    }
    else
    {
        m_depthShader = gl::Shader::compileFiles(shaderDir / "passes/depth_prepass.vert", shaderDir / "passes/depth_prepass.frag", shaderDir);
    }
}

void DepthPrePass::execute(const RenderContext &ctx, RenderResources &res)
{
    if (m_stage == DepthStage::Pass2RescueOpaque && !ctx.features.enableHzb)
    {
        return;
    }
    res.gbufferFbo().bind();
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);

    // Reversed-Z depth-only pre-pass: clear depth to 0.0 (far) in Pass 1, pass GREATER
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_GREATER);

    if (m_stage == DepthStage::Pass1Opaque)
    {
        glClearDepth(0.0);
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    if (m_stage == DepthStage::Grass)
    {
        if (res.grass().instanceCount() > 0)
        {
            glDisable(GL_CULL_FACE);
            m_grassDepthShader.use();
            res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
            res.grass().drawIndirect(0, 2);
            glEnable(GL_CULL_FACE);
        }
    }
    else
    {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        const MegaScene &scene = res.scene();
        if (scene.meshCount() > 0 && scene.objectCount() > 0)
        {
            m_depthShader.use();
            res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
            scene.bindSceneBuffers(0, 1, 2, 3);
            if (m_stage == DepthStage::Pass2RescueOpaque)
            {
                scene.bindRescueBuffers(2, 3);
                scene.bindRescueDrawIndirectBuffer();
            }
            else
            {
                scene.bindDrawIndirectBuffer();
            }

            scene.bindMegaVao();
            glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(scene.meshCount()), 0);
            glBindVertexArray(0);
        }
    }

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

} // namespace game::render
