#include "game/render/passes/VolumetricFogPass.hpp"

#include <glad/gl.h>

namespace game::render
{

VolumetricFogPass::VolumetricFogPass(const std::filesystem::path &shaderDir)
{
    reloadShaders(shaderDir);
}

void VolumetricFogPass::reloadShaders(const std::filesystem::path &shaderDir)
{
    m_injectShader = gl::Shader::compileComputeFile(shaderDir / "passes/fog_inject.comp", shaderDir);
    m_accumulateShader = gl::Shader::compileComputeFile(shaderDir / "passes/fog_accumulate.comp", shaderDir);
}

void VolumetricFogPass::execute(const RenderContext &ctx, RenderResources &res)
{
    if (!ctx.features.enableFog)
    {
        return;
    }
    const int writeIdx = res.fogWriteIndex();
    const int prevIdx = 1 - writeIdx;

    const GLuint gx = (kFogVolumeWidth + 7u) / 8u;
    const GLuint gy = (kFogVolumeHeight + 7u) / 8u;

    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);

    // Inject density, anisotropic moonlight/torch scattering, GI ambient, and temporal history
    m_injectShader.use();
    res.lightsSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 4);
    res.clusterCountsSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 5);
    res.clusterIndicesSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 6);
    glBindTextureUnit(0, res.fogVolumeTexId(prevIdx));
    glBindTextureUnit(1, res.sampledShadowDepthTexId(ctx.features.enableShadows));
    glBindTextureUnit(2, res.sampledGiShTexId(0, ctx.features.enableGi));
    glBindTextureUnit(3, res.sampledGiShTexId(1, ctx.features.enableGi));
    glBindTextureUnit(4, res.sampledGiShTexId(2, ctx.features.enableGi));
    glBindTextureUnit(5, res.mazeGridTexId());
    glBindImageTexture(0, res.fogVolumeTexId(writeIdx), 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glDispatchCompute(gx, gy, kFogVolumeDepth);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // March front-to-back across froxel slices with analytical Beer-Lambert transmittance integration
    m_accumulateShader.use();
    glBindTextureUnit(0, res.fogVolumeTexId(writeIdx));
    glBindImageTexture(0, res.fogAccumTexId(), 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glDispatchCompute(gx, gy, 1u);
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    res.advanceFogHistory();
}

} // namespace game::render
