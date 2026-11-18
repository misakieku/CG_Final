#include "game/render/passes/CullingPass.hpp"

#include <glad/gl.h>

namespace game::render
{

CullingPass::CullingPass(const std::filesystem::path &shaderDir, bool cullShadow)
    : m_cullShadow(cullShadow)
{
    reloadShaders(shaderDir);
}

void CullingPass::reloadShaders(const std::filesystem::path &shaderDir)
{
    gl::Shader compiled = gl::Shader::compileComputeFile(shaderDir / "passes/culling.comp", shaderDir);
    gl::Shader grassCompiled;
    if (!m_cullShadow)
    {
        grassCompiled = gl::Shader::compileComputeFile(shaderDir / "passes/grass_cull.comp", shaderDir);
    }
    m_cullShader = std::move(compiled);
    if (!m_cullShadow)
    {
        m_grassCullShader = std::move(grassCompiled);
    }
}

void CullingPass::execute(const RenderContext &ctx, RenderResources &res)
{
    (void)ctx;
    const MegaScene &scene = res.scene();
    if (scene.objectCount() > 0 && scene.meshCount() > 0)
    {
        scene.resetIndirectCommands();
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

        m_cullShader.use();
        m_cullShader.setInt("uCullShadow", m_cullShadow ? 1 : 0);
        res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
        scene.bindSceneBuffers(0, 1, 2, 3);

        const GLuint groupsX = (scene.objectCount() + 63u) / 64u;
        glDispatchCompute(groupsX, 1, 1);
        glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    }

    if (!m_cullShadow && res.grass().instanceCount() > 0)
    {
        const GrassField &grass = res.grass();
        grass.resetIndirectCommand();
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

        m_grassCullShader.use();
        m_grassCullShader.setInt("uGrassCount", static_cast<int>(grass.instanceCount()));
        res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
        grass.bindForCull(0, 1, 2);

        const GLuint grassGroups = (grass.instanceCount() + 63u) / 64u;
        glDispatchCompute(grassGroups, 1, 1);
        glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    }
}

} // namespace game::render
