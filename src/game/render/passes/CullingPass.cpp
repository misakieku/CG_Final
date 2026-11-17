#include "game/render/passes/CullingPass.hpp"

#include <glad/gl.h>

namespace game::render
{

CullingPass::CullingPass(const std::filesystem::path &shaderDir)
{
    reloadShaders(shaderDir);
}

void CullingPass::reloadShaders(const std::filesystem::path &shaderDir)
{
    gl::Shader compiled = gl::Shader::compileComputeFile(shaderDir / "passes/culling.comp", shaderDir);
    m_cullShader = std::move(compiled);
}

void CullingPass::execute(const RenderContext &ctx, RenderResources &res)
{
    (void)ctx;
    const MegaScene &scene = res.scene();
    if (scene.objectCount() == 0 || scene.meshCount() == 0)
    {
        return;
    }

    scene.resetIndirectCommands();
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

    m_cullShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    scene.bindSceneBuffers(0, 1, 2, 3);

    const GLuint groupsX = (scene.objectCount() + 63u) / 64u;
    glDispatchCompute(groupsX, 1, 1);
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
}

} // namespace game::render
