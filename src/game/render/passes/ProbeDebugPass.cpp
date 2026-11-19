#include "game/render/passes/ProbeDebugPass.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <stdexcept>
#include <vector>

namespace game::render
{

ProbeDebugPass::ProbeDebugPass(const std::filesystem::path &shaderDir)
{
    reloadShaders(shaderDir);
    buildSphereMesh();
}

ProbeDebugPass::~ProbeDebugPass()
{
    if (m_vao != 0)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
}

void ProbeDebugPass::reloadShaders(const std::filesystem::path &shaderDir)
{
    gl::Shader compiled = gl::Shader::compileFiles(shaderDir / "passes/gi_probe_debug.vert", shaderDir / "passes/gi_probe_debug.frag", shaderDir);
    m_probeShader = std::move(compiled);
}

void ProbeDebugPass::buildSphereMesh()
{
    constexpr int kSlices = 32;
    constexpr int kRings = 16;

    std::vector<glm::vec3> vertices;
    vertices.reserve(static_cast<std::size_t>((kSlices + 1) * (kRings + 1)));
    for (int r = 0; r <= kRings; ++r)
    {
        const float v = static_cast<float>(r) / static_cast<float>(kRings);
        const float theta = v * glm::pi<float>();
        const float sinT = std::sin(theta);
        const float cosT = std::cos(theta);
        for (int s = 0; s <= kSlices; ++s)
        {
            const float u = static_cast<float>(s) / static_cast<float>(kSlices);
            const float phi = u * glm::two_pi<float>();
            vertices.emplace_back(sinT * std::cos(phi), cosT, sinT * std::sin(phi));
        }
    }

    std::vector<std::uint32_t> indices;
    indices.reserve(static_cast<std::size_t>(kSlices * kRings * 6));
    const int stride = kSlices + 1;
    for (int r = 0; r < kRings; ++r)
    {
        for (int s = 0; s < kSlices; ++s)
        {
            const std::uint32_t i00 = static_cast<std::uint32_t>(r * stride + s);
            const std::uint32_t i10 = i00 + 1u;
            const std::uint32_t i01 = i00 + static_cast<std::uint32_t>(stride);
            const std::uint32_t i11 = i01 + 1u;

            indices.push_back(i00);
            indices.push_back(i10);
            indices.push_back(i01);

            indices.push_back(i10);
            indices.push_back(i11);
            indices.push_back(i01);
        }
    }

    m_indexCount = static_cast<std::uint32_t>(indices.size());
    m_vbo = GpuBuffer::createImmutableFromSpan<glm::vec3>(vertices);
    m_ibo = GpuBuffer::createImmutableFromSpan<std::uint32_t>(indices);

    glCreateVertexArrays(1, &m_vao);
    if (m_vao == 0)
    {
        throw std::runtime_error("glCreateVertexArrays failed for ProbeDebugPass");
    }
    glVertexArrayVertexBuffer(m_vao, 0, m_vbo.id(), 0, sizeof(glm::vec3));
    glVertexArrayElementBuffer(m_vao, m_ibo.id());
    glEnableVertexArrayAttrib(m_vao, 0);
    glVertexArrayAttribFormat(m_vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(m_vao, 0, 0);
}

void ProbeDebugPass::execute(const RenderContext &ctx, RenderResources &res)
{
    if (!ctx.features.enableProbeDebug || !res.hasGiGrid() || m_indexCount == 0)
    {
        return;
    }

    const glm::uvec3 dims = res.giProbeDims();
    const std::uint32_t totalProbes = dims.x * dims.y * dims.z;
    if (totalProbes == 0)
    {
        return;
    }

    res.hdrDepthFbo().bind();
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_GREATER);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);

    m_probeShader.use();
    m_probeShader.setFloat("uProbeRadius", ctx.probeDebugRadius);
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    res.giProbeMetaSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 7);
    res.giActiveFlagSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 8);
    glBindTextureUnit(0, res.sampledGiShTexId(0, ctx.features.enableGi));
    glBindTextureUnit(1, res.sampledGiShTexId(1, ctx.features.enableGi));
    glBindTextureUnit(2, res.sampledGiShTexId(2, ctx.features.enableGi));

    glBindVertexArray(m_vao);
    glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(m_indexCount), GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(totalProbes));
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
}

} // namespace game::render
