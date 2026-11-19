#include "game/render/MegaScene.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <map>
#include <random>
#include <stdexcept>
#include <utility>

namespace game::render
{

namespace
{

struct LocalBounds
{
    glm::vec4 sphere{0.0f};
    glm::vec4 aabbMin{0.0f};
    glm::vec4 aabbMax{0.0f};
};

[[nodiscard]] LocalBounds computeLocalBoundingBounds(std::span<const SceneVertex> vertices)
{
    if (vertices.empty())
    {
        return {};
    }
    glm::vec3 minP = vertices[0].position;
    glm::vec3 maxP = vertices[0].position;
    for (const SceneVertex &v : vertices)
    {
        minP = glm::min(minP, v.position);
        maxP = glm::max(maxP, v.position);
    }
    const glm::vec3 center = 0.5f * (minP + maxP);
    float maxDistSq = 0.0f;
    for (const SceneVertex &v : vertices)
    {
        const glm::vec3 d = v.position - center;
        maxDistSq = std::max(maxDistSq, glm::dot(d, d));
    }
    return LocalBounds{
        .sphere = glm::vec4(center, std::sqrt(maxDistSq)),
        .aabbMin = glm::vec4(minP, 1.0f),
        .aabbMax = glm::vec4(maxP, 1.0f),
    };
}

[[nodiscard]] glm::vec4 transformBoundingSphere(const glm::vec4 &localSphere, const glm::mat4 &model)
{
    const glm::vec3 worldCenter = glm::vec3(model * glm::vec4(glm::vec3(localSphere), 1.0f));
    const float sx = glm::length(glm::vec3(model[0]));
    const float sy = glm::length(glm::vec3(model[1]));
    const float sz = glm::length(glm::vec3(model[2]));
    const float maxScale = std::max({sx, sy, sz});
    return glm::vec4(worldCenter, localSphere.w * maxScale);
}

} // namespace

MegaScene::~MegaScene()
{
    destroyVao();
}

MegaScene::MegaScene(MegaScene &&other) noexcept
    : m_cpuVertices(std::move(other.m_cpuVertices)),
      m_cpuIndices(std::move(other.m_cpuIndices)),
      m_meshes(std::move(other.m_meshes)),
      m_meshInstanceCounts(std::move(other.m_meshInstanceCounts)),
      m_objects(std::move(other.m_objects)),
      m_waterObjectIds(std::move(other.m_waterObjectIds)),
      m_vao(std::exchange(other.m_vao, 0)),
      m_generation(std::exchange(other.m_generation, 0)),
      m_vbo(std::move(other.m_vbo)),
      m_ibo(std::move(other.m_ibo)),
      m_meshSpansSSBO(std::move(other.m_meshSpansSSBO)),
      m_objectsSSBO(std::move(other.m_objectsSSBO)),
      m_cmdTemplateSSBO(std::move(other.m_cmdTemplateSSBO)),
      m_workingCmdsSSBO(std::move(other.m_workingCmdsSSBO)),
      m_visibleIndicesSSBO(std::move(other.m_visibleIndicesSSBO)),
      m_objectCullStateSSBO(std::move(other.m_objectCullStateSSBO)),
      m_rescueCmdsSSBO(std::move(other.m_rescueCmdsSSBO)),
      m_rescueVisibleIndicesSSBO(std::move(other.m_rescueVisibleIndicesSSBO))
{
}

MegaScene &MegaScene::operator=(MegaScene &&other) noexcept
{
    if (this != &other)
    {
        destroyVao();
        m_cpuVertices = std::move(other.m_cpuVertices);
        m_cpuIndices = std::move(other.m_cpuIndices);
        m_meshes = std::move(other.m_meshes);
        m_meshInstanceCounts = std::move(other.m_meshInstanceCounts);
        m_objects = std::move(other.m_objects);
        m_waterObjectIds = std::move(other.m_waterObjectIds);
        m_vao = std::exchange(other.m_vao, 0);
        m_generation = std::exchange(other.m_generation, 0);
        m_vbo = std::move(other.m_vbo);
        m_ibo = std::move(other.m_ibo);
        m_meshSpansSSBO = std::move(other.m_meshSpansSSBO);
        m_objectsSSBO = std::move(other.m_objectsSSBO);
        m_cmdTemplateSSBO = std::move(other.m_cmdTemplateSSBO);
        m_workingCmdsSSBO = std::move(other.m_workingCmdsSSBO);
        m_visibleIndicesSSBO = std::move(other.m_visibleIndicesSSBO);
        m_objectCullStateSSBO = std::move(other.m_objectCullStateSSBO);
        m_rescueCmdsSSBO = std::move(other.m_rescueCmdsSSBO);
        m_rescueVisibleIndicesSSBO = std::move(other.m_rescueVisibleIndicesSSBO);
    }
    return *this;
}

std::uint32_t MegaScene::registerMesh(std::span<const SceneVertex> vertices, std::span<const std::uint32_t> indices)
{
    const std::uint32_t meshId = static_cast<std::uint32_t>(m_meshes.size());
    const LocalBounds bounds = computeLocalBoundingBounds(vertices);

    MeshSpan span;
    span.boundingSphere = bounds.sphere;
    span.aabbMin = bounds.aabbMin;
    span.aabbMax = bounds.aabbMax;
    span.indexCount = static_cast<std::uint32_t>(indices.size());
    span.firstIndex = static_cast<std::uint32_t>(m_cpuIndices.size());
    span.baseVertex = static_cast<std::int32_t>(m_cpuVertices.size());

    m_cpuVertices.insert(m_cpuVertices.end(), vertices.begin(), vertices.end());
    m_cpuIndices.insert(m_cpuIndices.end(), indices.begin(), indices.end());
    m_meshes.push_back(span);
    m_meshInstanceCounts.push_back(0);
    return meshId;
}

std::uint32_t MegaScene::registerDatMesh(const cg::DatMeshData &datMesh)
{
    float minY = 0.0f;
    float maxY = 1.0f;
    if (!datMesh.positions.empty())
    {
        minY = maxY = datMesh.positions[0].y;
        for (const glm::vec3 &p : datMesh.positions)
        {
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
    }
    const float invHeight = 1.0f / std::max(1e-4f, maxY - minY);

    std::vector<SceneVertex> verts;
    verts.reserve(datMesh.positions.size() + 64u);
    for (std::size_t i = 0; i < datMesh.positions.size(); ++i)
    {
        const glm::vec3 &p = datMesh.positions[i];
        const glm::vec3 n = (i < datMesh.normals.size()) ? datMesh.normals[i] : glm::vec3(0.0f, 1.0f, 0.0f);
        const float u = (std::atan2(p.z, p.x) + glm::pi<float>()) / glm::two_pi<float>();
        const float v = (p.y - minY) * invHeight;
        verts.push_back(SceneVertex{p, n, glm::vec2(u, v)});
    }

    // Duplicate vertices across the atan2 +/-pi seam so UV.x interpolates continuously [0.97 -> 1.00] without derivative spikes
    std::vector<std::uint32_t> fixedIndices(datMesh.indices.begin(), datMesh.indices.end());
    for (std::size_t t = 0; t + 2 < fixedIndices.size(); t += 3)
    {
        const std::uint32_t i0 = fixedIndices[t + 0];
        const std::uint32_t i1 = fixedIndices[t + 1];
        const std::uint32_t i2 = fixedIndices[t + 2];
        if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size())
        {
            continue;
        }
        const float u0 = verts[i0].uv.x;
        const float u1 = verts[i1].uv.x;
        const float u2 = verts[i2].uv.x;
        const float maxU = std::max({u0, u1, u2});
        const float minU = std::min({u0, u1, u2});
        if (maxU - minU > 0.5f)
        {
            for (std::size_t k = 0; k < 3; ++k)
            {
                const std::uint32_t idx = fixedIndices[t + k];
                if (verts[idx].uv.x < 0.5f)
                {
                    SceneVertex dup = verts[idx];
                    dup.uv.x += 1.0f;
                    fixedIndices[t + k] = static_cast<std::uint32_t>(verts.size());
                    verts.push_back(dup);
                }
            }
        }
    }
    return registerMesh(verts, fixedIndices);
}

std::uint32_t MegaScene::addObject(std::uint32_t meshId, std::uint32_t materialId, const glm::mat4 &model, std::uint32_t flags)
{
    if (meshId >= m_meshes.size())
    {
        throw std::out_of_range("MegaScene::addObject invalid meshId");
    }

    const std::uint32_t objectId = static_cast<std::uint32_t>(m_objects.size());
    DrawObjectData obj;
    obj.model = model;
    obj.normalMatrix = glm::mat4(glm::transpose(glm::inverse(glm::mat3(model))));
    obj.worldSphere = transformBoundingSphere(m_meshes[meshId].boundingSphere, model);
    obj.meshId = meshId;
    obj.materialId = materialId;
    obj.flags = flags;

    m_objects.push_back(obj);
    ++m_meshInstanceCounts[meshId];
    if ((flags & kObjectWaterFlag) != 0u)
    {
        m_waterObjectIds.push_back(objectId);
    }
    return objectId;
}

void MegaScene::buildGpuBuffers()
{
    destroyVao();

    m_vbo = GpuBuffer::createImmutableFromSpan<SceneVertex>(m_cpuVertices);
    m_ibo = GpuBuffer::createImmutableFromSpan<std::uint32_t>(m_cpuIndices);

    // Pure OpenGL 4.5 DSA VAO setup (zero glBindVertexArray/glBindBuffer needed for layout)
    glCreateVertexArrays(1, &m_vao);
    if (m_vao == 0)
    {
        throw std::runtime_error("glCreateVertexArrays failed");
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

    // Prefix-sum bucket offsets so every meshId gets a contiguous slice in VisibleInstancesSSBO
    std::vector<DrawElementsIndirectCommand> cmdTemplates(m_meshes.size());
    std::uint32_t runningOffset = 0;
    for (std::size_t i = 0; i < m_meshes.size(); ++i)
    {
        m_meshes[i].baseInstanceOffset = runningOffset;
        cmdTemplates[i] = DrawElementsIndirectCommand{
            .count = m_meshes[i].indexCount,
            .instanceCount = 0,
            .firstIndex = m_meshes[i].firstIndex,
            .baseVertex = m_meshes[i].baseVertex,
            .baseInstance = runningOffset,
        };
        runningOffset += m_meshInstanceCounts[i];
    }

    m_meshSpansSSBO = GpuBuffer::createImmutableFromSpan<MeshSpan>(m_meshes);
    m_objectsSSBO = GpuBuffer::createDynamic(m_objects.size() * sizeof(DrawObjectData), m_objects.data());
    m_cmdTemplateSSBO = GpuBuffer::createImmutableFromSpan<DrawElementsIndirectCommand>(cmdTemplates);
    m_workingCmdsSSBO = GpuBuffer::createImmutable(cmdTemplates.size() * sizeof(DrawElementsIndirectCommand));
    m_visibleIndicesSSBO = GpuBuffer::createImmutable(std::max<std::uint32_t>(1, runningOffset) * sizeof(std::uint32_t));
    m_objectCullStateSSBO = GpuBuffer::createImmutable(std::max<std::size_t>(1, m_objects.size()) * sizeof(std::uint32_t));
    m_objectCullStateSSBO.clearUint32(0u);
    m_rescueCmdsSSBO = GpuBuffer::createImmutable(cmdTemplates.size() * sizeof(DrawElementsIndirectCommand));
    m_rescueVisibleIndicesSSBO = GpuBuffer::createImmutable(std::max<std::uint32_t>(1, runningOffset) * sizeof(std::uint32_t));

    static std::atomic<std::uint64_t> s_nextGeneration{1u};
    m_generation = s_nextGeneration.fetch_add(1u, std::memory_order_relaxed);

    // CPU geometry buffers are redundant once uploaded to GPU MegaVBO / MegaIBO
    std::vector<SceneVertex>{}.swap(m_cpuVertices);
    std::vector<std::uint32_t>{}.swap(m_cpuIndices);
}

void MegaScene::updateObjectTransform(std::uint32_t objectId, const glm::mat4 &model)
{
    if (objectId >= m_objects.size())
    {
        throw std::out_of_range("MegaScene::updateObjectTransform invalid objectId");
    }
    DrawObjectData &obj = m_objects[objectId];
    obj.model = model;
    obj.normalMatrix = glm::mat4(glm::transpose(glm::inverse(glm::mat3(model))));
    obj.worldSphere = transformBoundingSphere(m_meshes[obj.meshId].boundingSphere, model);

    if (m_objectsSSBO)
    {
        m_objectsSSBO.upload(static_cast<std::size_t>(objectId) * sizeof(DrawObjectData), sizeof(DrawObjectData), &obj);
    }
}

void MegaScene::setObjectVisible(std::uint32_t objectId, bool visible)
{
    if (objectId >= m_objects.size())
    {
        throw std::out_of_range("MegaScene::setObjectVisible invalid objectId");
    }
    DrawObjectData &obj = m_objects[objectId];
    if (visible)
    {
        obj.flags |= kObjectVisibleFlag;
    }
    else
    {
        obj.flags &= ~kObjectVisibleFlag;
    }

    if (m_objectsSSBO)
    {
        const std::size_t flagsOffset =
            static_cast<std::size_t>(objectId) * sizeof(DrawObjectData) + offsetof(DrawObjectData, flags);
        m_objectsSSBO.upload(flagsOffset, sizeof(obj.flags), &obj.flags);
    }
}

void MegaScene::resetIndirectCommands() const
{
    m_workingCmdsSSBO.copyFrom(m_cmdTemplateSSBO, 0, 0, m_cmdTemplateSSBO.sizeBytes());
}

void MegaScene::resetRescueIndirectCommands() const
{
    m_rescueCmdsSSBO.copyFrom(m_cmdTemplateSSBO, 0, 0, m_cmdTemplateSSBO.sizeBytes());
}

void MegaScene::bindMegaVao() const noexcept
{
    glBindVertexArray(m_vao);
}

void MegaScene::bindSceneBuffers(GLuint objectsSlot, GLuint meshesSlot, GLuint cmdsSlot, GLuint visibleIndicesSlot) const noexcept
{
    m_objectsSSBO.bindBase(GL_SHADER_STORAGE_BUFFER, objectsSlot);
    m_meshSpansSSBO.bindBase(GL_SHADER_STORAGE_BUFFER, meshesSlot);
    m_workingCmdsSSBO.bindBase(GL_SHADER_STORAGE_BUFFER, cmdsSlot);
    m_visibleIndicesSSBO.bindBase(GL_SHADER_STORAGE_BUFFER, visibleIndicesSlot);
}

void MegaScene::bindCullStateBuffer(GLuint slot) const noexcept
{
    m_objectCullStateSSBO.bindBase(GL_SHADER_STORAGE_BUFFER, slot);
}

void MegaScene::bindRescueBuffers(GLuint cmdsSlot, GLuint visibleIndicesSlot) const noexcept
{
    m_rescueCmdsSSBO.bindBase(GL_SHADER_STORAGE_BUFFER, cmdsSlot);
    m_rescueVisibleIndicesSSBO.bindBase(GL_SHADER_STORAGE_BUFFER, visibleIndicesSlot);
}

void MegaScene::bindDrawIndirectBuffer() const noexcept
{
    m_workingCmdsSSBO.bind(GL_DRAW_INDIRECT_BUFFER);
}

void MegaScene::bindRescueDrawIndirectBuffer() const noexcept
{
    m_rescueCmdsSSBO.bind(GL_DRAW_INDIRECT_BUFFER);
}

void MegaScene::destroyVao() noexcept
{
    if (m_vao != 0)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
}

std::uint32_t registerUnitBoxMesh(MegaScene &scene, float halfExtent, float uvScale)
{
    const float h = halfExtent;
    const float u = uvScale;
    const SceneVertex verts[] = {
        // +Z
        {{-h, -h, h}, {0, 0, 1}, {0, 0}}, {{h, -h, h}, {0, 0, 1}, {u, 0}}, {{h, h, h}, {0, 0, 1}, {u, u}}, {{-h, h, h}, {0, 0, 1}, {0, u}},
        // -Z
        {{h, -h, -h}, {0, 0, -1}, {0, 0}}, {{-h, -h, -h}, {0, 0, -1}, {u, 0}}, {{-h, h, -h}, {0, 0, -1}, {u, u}}, {{h, h, -h}, {0, 0, -1}, {0, u}},
        // +X
        {{h, -h, h}, {1, 0, 0}, {0, 0}}, {{h, -h, -h}, {1, 0, 0}, {u, 0}}, {{h, h, -h}, {1, 0, 0}, {u, u}}, {{h, h, h}, {1, 0, 0}, {0, u}},
        // -X
        {{-h, -h, -h}, {-1, 0, 0}, {0, 0}}, {{-h, -h, h}, {-1, 0, 0}, {u, 0}}, {{-h, h, h}, {-1, 0, 0}, {u, u}}, {{-h, h, -h}, {-1, 0, 0}, {0, u}},
        // +Y
        {{-h, h, h}, {0, 1, 0}, {0, 0}}, {{h, h, h}, {0, 1, 0}, {u, 0}}, {{h, h, -h}, {0, 1, 0}, {u, u}}, {{-h, h, -h}, {0, 1, 0}, {0, u}},
        // -Y
        {{-h, -h, -h}, {0, -1, 0}, {0, 0}}, {{h, -h, -h}, {0, -1, 0}, {u, 0}}, {{h, -h, h}, {0, -1, 0}, {u, u}}, {{-h, -h, h}, {0, -1, 0}, {0, u}},
    };

    std::vector<std::uint32_t> indices;
    indices.reserve(36);
    for (std::uint32_t face = 0; face < 6; ++face)
    {
        const std::uint32_t base = face * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }
    return scene.registerMesh(verts, indices);
}

std::uint32_t registerBeveledBoxMesh(MegaScene &scene, float halfExtent, float bevel)
{
    const float h = halfExtent;
    const float a = std::max(0.01f, halfExtent - bevel);

    std::vector<SceneVertex> verts;
    std::vector<std::uint32_t> indices;

    auto addTri = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2)
    {
        glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
        const float lenSq = glm::dot(n, n);
        if (lenSq < 1e-12f)
        {
            return;
        }
        n /= std::sqrt(lenSq);
        if (glm::dot(n, p0 + p1 + p2) < 0.0f)
        {
            std::swap(p1, p2);
            n = -n;
        }
        const auto base = static_cast<std::uint32_t>(verts.size());
        auto makeUv = [&](glm::vec3 p) -> glm::vec2
        {
            const glm::vec3 an = glm::abs(n);
            if (an.x >= an.y && an.x >= an.z)
            {
                return glm::vec2(p.z, p.y) + 0.5f;
            }
            if (an.y >= an.x && an.y >= an.z)
            {
                return glm::vec2(p.x, p.z) + 0.5f;
            }
            return glm::vec2(p.x, p.y) + 0.5f;
        };
        verts.push_back({p0, n, makeUv(p0)});
        verts.push_back({p1, n, makeUv(p1)});
        verts.push_back({p2, n, makeUv(p2)});
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
    };

    auto addQuad = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3)
    {
        addTri(p0, p1, p2);
        addTri(p0, p2, p3);
    };

    auto octPointX = [&](int sx, int sy, int sz) { return glm::vec3(sx * h, sy * a, sz * a); };
    auto octPointY = [&](int sx, int sy, int sz) { return glm::vec3(sx * a, sy * h, sz * a); };
    auto octPointZ = [&](int sx, int sy, int sz) { return glm::vec3(sx * a, sy * a, sz * h); };

    // 6 main faces
    for (int s : {-1, 1})
    {
        addQuad(octPointX(s, -1, -1), octPointX(s, 1, -1), octPointX(s, 1, 1), octPointX(s, -1, 1));
        addQuad(octPointY(-1, s, -1), octPointY(1, s, -1), octPointY(1, s, 1), octPointY(-1, s, 1));
        addQuad(octPointZ(-1, -1, s), octPointZ(1, -1, s), octPointZ(1, 1, s), octPointZ(-1, 1, s));
    }

    // beveled edges
    for (int s0 : {-1, 1})
    {
        for (int s1 : {-1, 1})
        {
            addQuad(octPointX(s0, s1, -1), octPointX(s0, s1, 1), octPointY(s0, s1, 1), octPointY(s0, s1, -1));
            addQuad(octPointY(-1, s0, s1), octPointY(1, s0, s1), octPointZ(1, s0, s1), octPointZ(-1, s0, s1));
            addQuad(octPointZ(s0, -1, s1), octPointZ(s0, 1, s1), octPointX(s0, 1, s1), octPointX(s0, -1, s1));
        }
    }

    // 8 corner bevel triangles
    for (int sx : {-1, 1})
    {
        for (int sy : {-1, 1})
        {
            for (int sz : {-1, 1})
            {
                addTri(octPointX(sx, sy, sz), octPointY(sx, sy, sz), octPointZ(sx, sy, sz));
            }
        }
    }

    return scene.registerMesh(verts, indices);
}

std::uint32_t registerPlaneMesh(MegaScene &scene, float halfExtent, float uvRepeat)
{
    const float h = halfExtent;
    const SceneVertex verts[] = {
        {{-h, 0.0f, h}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{h, 0.0f, h}, {0.0f, 1.0f, 0.0f}, {uvRepeat, 0.0f}},
        {{h, 0.0f, -h}, {0.0f, 1.0f, 0.0f}, {uvRepeat, uvRepeat}},
        {{-h, 0.0f, -h}, {0.0f, 1.0f, 0.0f}, {0.0f, uvRepeat}},
    };
    const std::uint32_t indices[] = {0, 1, 2, 0, 2, 3};
    return scene.registerMesh(verts, indices);
}

std::uint32_t registerRockMesh(MegaScene &scene, std::uint32_t seed)
{
    const float phi = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<glm::vec3> baseVerts = {
        {-1, phi, 0}, {1, phi, 0}, {-1, -phi, 0}, {1, -phi, 0},
        {0, -1, phi}, {0, 1, phi}, {0, -1, -phi}, {0, 1, -phi},
        {phi, 0, -1}, {phi, 0, 1}, {-phi, 0, -1}, {-phi, 0, 1},
    };
    for (glm::vec3 &v : baseVerts)
    {
        v = glm::normalize(v);
    }

    std::vector<std::array<std::uint32_t, 3>> faces = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1},
    };

    // 1-level icosphere subdivision for angular multi-faceted stones
    std::map<std::uint64_t, std::uint32_t> midpointCache;
    auto midpoint = [&](std::uint32_t a, std::uint32_t b) -> std::uint32_t
    {
        const std::uint64_t key = (static_cast<std::uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
        if (const auto it = midpointCache.find(key); it != midpointCache.end())
        {
            return it->second;
        }
        const auto idx = static_cast<std::uint32_t>(baseVerts.size());
        baseVerts.push_back(glm::normalize(0.5f * (baseVerts[a] + baseVerts[b])));
        midpointCache.emplace(key, idx);
        return idx;
    };

    std::vector<std::array<std::uint32_t, 3>> subFaces;
    subFaces.reserve(faces.size() * 4);
    for (const auto &f : faces)
    {
        const std::uint32_t m0 = midpoint(f[0], f[1]);
        const std::uint32_t m1 = midpoint(f[1], f[2]);
        const std::uint32_t m2 = midpoint(f[2], f[0]);
        subFaces.push_back({f[0], m0, m2});
        subFaces.push_back({f[1], m1, m0});
        subFaces.push_back({f[2], m2, m1});
        subFaces.push_back({m0, m1, m2});
    }

    // Displace each vertex deterministically so rocks look jagged & naturally squashed
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> jitterDist(0.68f, 1.24f);
    for (glm::vec3 &v : baseVerts)
    {
        v *= jitterDist(rng);
        v.y *= 0.62f;
    }

    std::vector<SceneVertex> verts;
    std::vector<std::uint32_t> indices;
    verts.reserve(subFaces.size() * 3);
    indices.reserve(subFaces.size() * 3);
    for (const auto &f : subFaces)
    {
        const glm::vec3 p0 = baseVerts[f[0]];
        const glm::vec3 p1 = baseVerts[f[1]];
        const glm::vec3 p2 = baseVerts[f[2]];
        const glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        const auto base = static_cast<std::uint32_t>(verts.size());
        verts.push_back({p0, n, {0.0f, 0.0f}});
        verts.push_back({p1, n, {1.0f, 0.0f}});
        verts.push_back({p2, n, {0.5f, 1.0f}});
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
    }

    return scene.registerMesh(verts, indices);
}

} // namespace game::render
