#include "game/render/GrassField.hpp"

#include <glm/gtc/constants.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace game::render
{

namespace
{

struct BladeSpec
{
    glm::vec2 baseOffset{0.0f};
    float yawOffset = 0.0f;
    float heightMul = 1.0f;
    float widthMul = 1.0f;
    float leanOut = 0.08f;
};

void appendClumpBlade(std::vector<SceneVertex> &vertices, std::vector<std::uint32_t> &indices, const BladeSpec &spec)
{
    constexpr int kSegments = 3;
    constexpr float kRootSink = -0.008f;
    constexpr float kBaseHeight = 0.27f;
    constexpr float kBaseHalfWidth = 0.021f;

    const glm::vec3 forward(std::cos(spec.yawOffset), 0.0f, std::sin(spec.yawOffset));
    const glm::vec3 side(-forward.z, 0.0f, forward.x);
    const std::uint32_t baseVert = static_cast<std::uint32_t>(vertices.size());

    for (int s = 0; s < kSegments; ++s)
    {
        const float t = static_cast<float>(s) / static_cast<float>(kSegments);
        const float width = kBaseHalfWidth * spec.widthMul * (1.0f - 0.78f * t * t);
        const float y = glm::mix(kRootSink, kBaseHeight * spec.heightMul, t);
        const float bend = (t * t) * spec.leanOut;

        const glm::vec3 spine(spec.baseOffset.x + forward.x * bend, y, spec.baseOffset.y + forward.z * bend);
        const glm::vec3 upBias = glm::normalize(glm::vec3(forward.x * 0.35f, 1.0f, forward.z * 0.35f));
        const glm::vec3 leftN = glm::normalize(upBias - side * 0.25f);
        const glm::vec3 rightN = glm::normalize(upBias + side * 0.25f);

        vertices.push_back({spine - side * width, leftN, glm::vec2(-1.0f, t)});
        vertices.push_back({spine + side * width, rightN, glm::vec2(1.0f, t)});
    }

    const glm::vec3 tipSpine(spec.baseOffset.x + forward.x * spec.leanOut, kBaseHeight * spec.heightMul, spec.baseOffset.y + forward.z * spec.leanOut);
    const glm::vec3 tipN = glm::normalize(glm::vec3(forward.x * 0.25f, 1.0f, forward.z * 0.25f));
    vertices.push_back({tipSpine, tipN, glm::vec2(0.0f, 1.0f)});

    for (int s = 0; s + 1 < kSegments; ++s)
    {
        const std::uint32_t r0 = baseVert + static_cast<std::uint32_t>(s * 2);
        const std::uint32_t r1 = r0 + 2u;
        indices.insert(indices.end(), {r0, r0 + 1u, r1 + 1u, r0, r1 + 1u, r1});
    }
    const std::uint32_t topRow = baseVert + static_cast<std::uint32_t>((kSegments - 1) * 2);
    const std::uint32_t tipIdx = baseVert + static_cast<std::uint32_t>(kSegments * 2);
    indices.insert(indices.end(), {topRow, topRow + 1u, tipIdx});
}

} // namespace

GrassField::~GrassField()
{
    destroyVao();
}

GrassField::GrassField(GrassField &&other) noexcept
    : m_instanceCount(std::exchange(other.m_instanceCount, 0u)),
      m_indexCount(std::exchange(other.m_indexCount, 0u)),
      m_vao(std::exchange(other.m_vao, 0u)),
      m_vbo(std::move(other.m_vbo)),
      m_ibo(std::move(other.m_ibo)),
      m_instancesSSBO(std::move(other.m_instancesSSBO)),
      m_visibleIndicesSSBO(std::move(other.m_visibleIndicesSSBO)),
      m_cmdTemplateSSBO(std::move(other.m_cmdTemplateSSBO)),
      m_workingCmdsSSBO(std::move(other.m_workingCmdsSSBO))
{
}

GrassField &GrassField::operator=(GrassField &&other) noexcept
{
    if (this != &other)
    {
        destroyVao();
        m_instanceCount = std::exchange(other.m_instanceCount, 0u);
        m_indexCount = std::exchange(other.m_indexCount, 0u);
        m_vao = std::exchange(other.m_vao, 0u);
        m_vbo = std::move(other.m_vbo);
        m_ibo = std::move(other.m_ibo);
        m_instancesSSBO = std::move(other.m_instancesSSBO);
        m_visibleIndicesSSBO = std::move(other.m_visibleIndicesSSBO);
        m_cmdTemplateSSBO = std::move(other.m_cmdTemplateSSBO);
        m_workingCmdsSSBO = std::move(other.m_workingCmdsSSBO);
    }
    return *this;
}

void GrassField::destroyVao() noexcept
{
    if (m_vao != 0)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
}

void GrassField::buildClumpGeometryIfNeeded()
{
    if (m_vao != 0)
    {
        return;
    }

    constexpr std::array<BladeSpec, 5> kClumpBlades = {{
        {.baseOffset = {0.012f, 0.008f}, .yawOffset = 0.25f, .heightMul = 1.05f, .widthMul = 1.05f, .leanOut = 0.115f},
        {.baseOffset = {-0.038f, 0.028f}, .yawOffset = 1.65f, .heightMul = 0.86f, .widthMul = 0.95f, .leanOut = 0.135f},
        {.baseOffset = {-0.020f, -0.040f}, .yawOffset = 3.20f, .heightMul = 0.96f, .widthMul = 0.90f, .leanOut = 0.125f},
        {.baseOffset = {0.042f, -0.018f}, .yawOffset = 4.65f, .heightMul = 0.72f, .widthMul = 0.85f, .leanOut = 0.140f},
        {.baseOffset = {0.002f, 0.044f}, .yawOffset = 5.75f, .heightMul = 0.62f, .widthMul = 0.82f, .leanOut = 0.120f},
    }};

    std::vector<SceneVertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(kClumpBlades.size() * 7u);
    indices.reserve(kClumpBlades.size() * 15u);

    for (const BladeSpec &blade : kClumpBlades)
    {
        appendClumpBlade(vertices, indices, blade);
    }

    m_indexCount = static_cast<std::uint32_t>(indices.size());
    m_vbo = GpuBuffer::createImmutableFromSpan<SceneVertex>(vertices);
    m_ibo = GpuBuffer::createImmutableFromSpan<std::uint32_t>(indices);

    glCreateVertexArrays(1, &m_vao);
    if (m_vao == 0)
    {
        throw std::runtime_error("GrassField: glCreateVertexArrays failed");
    }
    glVertexArrayVertexBuffer(m_vao, 0, m_vbo.id(), 0, sizeof(SceneVertex));
    glVertexArrayElementBuffer(m_vao, m_ibo.id());

    glEnableVertexArrayAttrib(m_vao, 0);
    glVertexArrayAttribFormat(m_vao, 0, 3, GL_FLOAT, GL_FALSE, static_cast<GLuint>(offsetof(SceneVertex, position)));
    glVertexArrayAttribBinding(m_vao, 0, 0);

    glEnableVertexArrayAttrib(m_vao, 1);
    glVertexArrayAttribFormat(m_vao, 1, 3, GL_FLOAT, GL_FALSE, static_cast<GLuint>(offsetof(SceneVertex, normal)));
    glVertexArrayAttribBinding(m_vao, 1, 0);

    glEnableVertexArrayAttrib(m_vao, 2);
    glVertexArrayAttribFormat(m_vao, 2, 2, GL_FLOAT, GL_FALSE, static_cast<GLuint>(offsetof(SceneVertex, uv)));
    glVertexArrayAttribBinding(m_vao, 2, 0);

    const DrawElementsIndirectCommand cmdTemplate{
        .count = m_indexCount,
        .instanceCount = 0u,
        .firstIndex = 0u,
        .baseVertex = 0,
        .baseInstance = 0u,
    };
    m_cmdTemplateSSBO = GpuBuffer::createImmutable(sizeof(DrawElementsIndirectCommand), &cmdTemplate);
    m_workingCmdsSSBO = GpuBuffer::createImmutable(sizeof(DrawElementsIndirectCommand));
}

void GrassField::uploadInstances(std::span<const GrassInstance> instances)
{
    if (instances.empty())
    {
        clear();
        return;
    }
    buildClumpGeometryIfNeeded();
    m_instanceCount = static_cast<std::uint32_t>(instances.size());
    m_instancesSSBO = GpuBuffer::createImmutableFromSpan<GrassInstance>(instances);
    m_visibleIndicesSSBO = GpuBuffer::createImmutable(instances.size() * sizeof(std::uint32_t));
}

void GrassField::clear() noexcept
{
    m_instanceCount = 0;
    m_instancesSSBO.reset();
    m_visibleIndicesSSBO.reset();
}

void GrassField::resetIndirectCommand() const
{
    if (m_instanceCount == 0 || !m_workingCmdsSSBO)
    {
        return;
    }
    m_workingCmdsSSBO.copyFrom(m_cmdTemplateSSBO, 0, 0, sizeof(DrawElementsIndirectCommand));
}

void GrassField::bindForCull(GLuint instancesSlot, GLuint cmdsSlot, GLuint visibleIndicesSlot) const noexcept
{
    m_instancesSSBO.bindBase(GL_SHADER_STORAGE_BUFFER, instancesSlot);
    m_workingCmdsSSBO.bindBase(GL_SHADER_STORAGE_BUFFER, cmdsSlot);
    m_visibleIndicesSSBO.bindBase(GL_SHADER_STORAGE_BUFFER, visibleIndicesSlot);
}

void GrassField::drawIndirect(GLuint instancesSlot, GLuint visibleIndicesSlot) const noexcept
{
    if (m_instanceCount == 0 || m_vao == 0)
    {
        return;
    }
    m_instancesSSBO.bindBase(GL_SHADER_STORAGE_BUFFER, instancesSlot);
    m_visibleIndicesSSBO.bindBase(GL_SHADER_STORAGE_BUFFER, visibleIndicesSlot);
    m_workingCmdsSSBO.bind(GL_DRAW_INDIRECT_BUFFER);
    glBindVertexArray(m_vao);
    glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

} // namespace game::render
