#include "game/render/passes/LightCullingPass.hpp"

#include <glad/gl.h>

namespace game::render
{

LightCullingPass::LightCullingPass(const std::filesystem::path &shaderDir)
{
    reloadShaders(shaderDir);
}

void LightCullingPass::reloadShaders(const std::filesystem::path &shaderDir)
{
    gl::Shader compiled = gl::Shader::compileComputeFile(shaderDir / "passes/light_culling.comp", shaderDir);
    m_cullingShader = std::move(compiled);
}

void LightCullingPass::execute(const RenderContext &ctx, RenderResources &res)
{
    (void)ctx;
    m_cullingShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    res.lightsSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 4);
    res.clusterCountsSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 5);
    res.clusterIndicesSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 6);

    const GLuint groupsX = (kTotalClusters + 63u) / 64u;
    glDispatchCompute(groupsX, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

} // namespace game::render
