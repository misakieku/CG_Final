#include "game/render/passes/CullingPass.hpp"

#include <glad/gl.h>

namespace game::render
{

CullingPass::CullingPass(const std::filesystem::path &shaderDir, CullingStage stage)
    : m_stage(stage)
{
    reloadShaders(shaderDir);
}

void CullingPass::reloadShaders(const std::filesystem::path &shaderDir)
{
    gl::Shader compiled = gl::Shader::compileComputeFile(shaderDir / "passes/culling.comp", shaderDir);
    gl::Shader grassCompiled;
    if (m_stage == CullingStage::MainPass2)
    {
        grassCompiled = gl::Shader::compileComputeFile(shaderDir / "passes/grass_cull.comp", shaderDir);
    }
    m_cullShader = std::move(compiled);
    if (m_stage == CullingStage::MainPass2)
    {
        m_grassCullShader = std::move(grassCompiled);
    }
}

void CullingPass::execute(const RenderContext &ctx, RenderResources &res)
{
    if (m_stage == CullingStage::Shadow && !ctx.features.enableShadows)
    {
        return;
    }

    const MegaScene &scene = res.scene();
    const bool runSceneCull = (m_stage != CullingStage::MainPass2) || ctx.features.enableHzb;
    if (runSceneCull && scene.objectCount() > 0 && scene.meshCount() > 0)
    {
        if (m_stage == CullingStage::MainPass2)
        {
            scene.resetRescueIndirectCommands();
        }
        else
        {
            scene.resetIndirectCommands();
        }
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

        m_cullShader.use();
        const int cullMode = (m_stage == CullingStage::Shadow) ? 0 : (m_stage == CullingStage::MainPass1 ? 1 : 2);
        m_cullShader.setInt("uCullMode", cullMode);
        res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
        scene.bindSceneBuffers(0, 1, 2, 3);
        if (m_stage != CullingStage::Shadow)
        {
            scene.bindCullStateBuffer(4);
            scene.bindRescueBuffers(5, 6);
            const GLuint hzbTex = (m_stage == CullingStage::MainPass1) ? res.prevHzbTexId() : res.currHzbTexId();
            glBindTextureUnit(0, hzbTex);
        }

        const GLuint groupsX = (scene.objectCount() + 63u) / 64u;
        glDispatchCompute(groupsX, 1, 1);
        glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    }

    if (m_stage == CullingStage::MainPass2 && res.grass().instanceCount() > 0)
    {
        const GrassField &grass = res.grass();
        grass.resetIndirectCommand();
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

        m_grassCullShader.use();
        m_grassCullShader.setInt("uGrassCount", static_cast<int>(grass.instanceCount()));
        res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
        grass.bindForCull(0, 1, 2);
        glBindTextureUnit(0, res.currHzbTexId());

        const GLuint grassGroups = (grass.instanceCount() + 63u) / 64u;
        glDispatchCompute(grassGroups, 1, 1);
        glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    }
}

} // namespace game::render
