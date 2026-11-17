#include "game/render/passes/WaterPass.hpp"

#include <glad/gl.h>

#include <cstdint>

namespace game::render
{

WaterPass::WaterPass(const std::filesystem::path &shaderDir)
{
    reloadShaders(shaderDir);
}

void WaterPass::reloadShaders(const std::filesystem::path &shaderDir)
{
    gl::Shader compiled = gl::Shader::compileFiles(shaderDir / "passes/water.vert", shaderDir / "passes/water.frag", shaderDir);
    m_waterShader = std::move(compiled);
}

void WaterPass::execute(const RenderContext &ctx, RenderResources &res)
{
    const MegaScene &scene = res.scene();
    const std::span<const std::uint32_t> waterIds = scene.waterObjectIds();
    if (waterIds.empty())
    {
        return;
    }

    res.hdrFbo().bind();
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    m_waterShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    scene.bindSceneBuffers(0, 1, 2, 3);
    res.lightsSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 4);
    res.clusterCountsSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 5);
    res.clusterIndicesSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 6);
    glBindTextureUnit(0, res.depthTex().id());

    scene.bindMegaVao();
    for (const std::uint32_t objId : waterIds)
    {
        const DrawObjectData &obj = scene.object(objId);
        if ((obj.flags & kObjectVisibleFlag) == 0u)
        {
            continue;
        }
        const MeshSpan &mesh = scene.meshSpan(obj.meshId);
        const void *indexOffset = reinterpret_cast<const void *>(static_cast<std::uintptr_t>(mesh.firstIndex) * sizeof(std::uint32_t));
        glDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES, static_cast<GLsizei>(mesh.indexCount), GL_UNSIGNED_INT, indexOffset, 1, mesh.baseVertex, objId);
    }

    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

} // namespace game::render
