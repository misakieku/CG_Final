#include "game/render/passes/PointShadowPass.hpp"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

#include <array>

namespace game::render
{

namespace
{

constexpr std::array<glm::vec3, 6> kCubemapDirs = {
    glm::vec3(1.0f, 0.0f, 0.0f),
    glm::vec3(-1.0f, 0.0f, 0.0f),
    glm::vec3(0.0f, 1.0f, 0.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(0.0f, 0.0f, 1.0f),
    glm::vec3(0.0f, 0.0f, -1.0f),
};

constexpr std::array<glm::vec3, 6> kCubemapUps = {
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(0.0f, 0.0f, 1.0f),
    glm::vec3(0.0f, 0.0f, -1.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),
};

[[nodiscard]] std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4 &vp) noexcept
{
    auto row = [&](int r) -> glm::vec4
    {
        return glm::vec4(vp[0][r], vp[1][r], vp[2][r], vp[3][r]);
    };

    const glm::vec4 r0 = row(0);
    const glm::vec4 r1 = row(1);
    const glm::vec4 r2 = row(2);
    const glm::vec4 r3 = row(3);

    std::array<glm::vec4, 6> planes = {
        r3 + r0,
        r3 - r0,
        r3 + r1,
        r3 - r1,
        r2,
        r3 - r2,
    };

    for (glm::vec4 &p : planes)
    {
        const float len = glm::length(glm::vec3(p));
        if (len > 1e-6f)
        {
            p /= len;
        }
    }
    return planes;
}

} // namespace

PointShadowPass::PointShadowPass(const std::filesystem::path &shaderDir)
{
    reloadShaders(shaderDir);
}

void PointShadowPass::reloadShaders(const std::filesystem::path &shaderDir)
{
    gl::Shader cullCompiled = gl::Shader::compileComputeFile(shaderDir / "passes/culling.comp", shaderDir);
    gl::Shader shadowCompiled = gl::Shader::compileFiles(shaderDir / "passes/point_shadow.vert", shaderDir / "passes/shadow.frag", shaderDir);
    m_cullShader = std::move(cullCompiled);
    m_pointShadowShader = std::move(shadowCompiled);
}

void PointShadowPass::execute(const RenderContext &ctx, RenderResources &res)
{
    if (!ctx.features.enablePointShadows)
    {
        return;
    }
    const MegaScene &scene = res.scene();
    if (scene.meshCount() == 0 || scene.objectCount() == 0)
    {
        return;
    }

    auto &slots = res.pointShadowSlots();
    bool anyDirty = false;
    for (const auto &slot : slots)
    {
        if (slot.dirty && slot.lightIndex >= 0)
        {
            anyDirty = true;
            break;
        }
    }
    if (!anyDirty)
    {
        return;
    }

    res.pointShadowFbo().bind();
    glViewport(0, 0, RenderResources::kPointShadowSize, RenderResources::kPointShadowSize);

    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.8f, 3.0f);
    glClearDepth(1.0);

    const GLuint groupsX = (scene.objectCount() + 63u) / 64u;
    const GLuint cubeArrayId = res.pointShadowCubeArrayTexId();

    for (int s = 0; s < RenderResources::kMaxPointShadowCasters; ++s)
    {
        RenderResources::PointShadowSlot &slot = slots[static_cast<std::size_t>(s)];
        if (!slot.dirty || slot.lightIndex < 0)
        {
            continue;
        }

        const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, RenderResources::kPointShadowNear, slot.radius);
        const glm::vec4 lightSphere(slot.pos, slot.radius);

        for (int face = 0; face < 6; ++face)
        {
            const glm::mat4 view = glm::lookAt(slot.pos, slot.pos + kCubemapDirs[static_cast<std::size_t>(face)], kCubemapUps[static_cast<std::size_t>(face)]);
            const glm::mat4 vp = proj * view;
            const std::array<glm::vec4, 6> planes = extractFrustumPlanes(vp);

            scene.resetIndirectCommands();
            glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

            m_cullShader.use();
            m_cullShader.setInt("uCullMode", 3);
            m_cullShader.setVec4Array("uPointShadowPlanes", planes);
            m_cullShader.setVec4("uPointShadowSphere", lightSphere);
            res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
            scene.bindSceneBuffers(0, 1, 2, 3);
            glDispatchCompute(groupsX, 1, 1);
            glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, cubeArrayId, 0, s * 6 + face);
            glClear(GL_DEPTH_BUFFER_BIT);

            m_pointShadowShader.use();
            m_pointShadowShader.setMat4("uPointShadowVP", vp);
            res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
            scene.bindSceneBuffers(0, 1, 2, 3);
            scene.bindMegaVao();
            scene.bindDrawIndirectBuffer();
            glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(scene.meshCount()), 0);
        }

        slot.dirty = false;
    }

    glBindVertexArray(0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glClearDepth(0.0);
    glDepthFunc(GL_GREATER);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

} // namespace game::render
