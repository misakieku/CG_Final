#pragma once

#include "common/dat_mesh.hpp"
#include "game/render/GpuBuffer.hpp"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace game::render
{

/// Vertex format stored in the global MegaVBO.
struct SceneVertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

/// std430-compatible mesh metadata for GPU culling and indirect bucket offsets.
struct alignas(16) MeshSpan
{
    glm::vec4 boundingSphere{0.0f}; // xyz = local center, w = radius
    std::uint32_t indexCount = 0;
    std::uint32_t firstIndex = 0;
    std::int32_t baseVertex = 0;
    std::uint32_t baseInstanceOffset = 0;
};

/// std430-compatible per-instance GPU scene record (strictly no vec3 members).
struct alignas(16) DrawObjectData
{
    glm::mat4 model{1.0f};
    glm::mat4 normalMatrix{1.0f};
    glm::vec4 worldSphere{0.0f}; // xyz = world center, w = world radius
    std::uint32_t meshId = 0;
    std::uint32_t materialId = 0;
    std::uint32_t flags = 0;
    std::uint32_t pad = 0;
};

/// Hardware-defined 20-byte DrawElementsIndirectCommand layout.
struct DrawElementsIndirectCommand
{
    std::uint32_t count = 0;
    std::uint32_t instanceCount = 0;
    std::uint32_t firstIndex = 0;
    std::int32_t baseVertex = 0;
    std::uint32_t baseInstance = 0;
};

/// Bitmask flags on DrawObjectData::flags.
inline constexpr std::uint32_t kObjectVisibleFlag = 1u << 0;
inline constexpr std::uint32_t kObjectWorldUVFlag = 1u << 1;
inline constexpr std::uint32_t kObjectWindSwayFlag = 1u << 2;

/// Persistent GPU scene holding all meshes in a single MegaVBO/MegaIBO and bucketed indirect buffers.
class MegaScene
{
public:
    MegaScene() = default;
    ~MegaScene();

    MegaScene(const MegaScene &) = delete;
    MegaScene &operator=(const MegaScene &) = delete;
    MegaScene(MegaScene &&other) noexcept;
    MegaScene &operator=(MegaScene &&other) noexcept;

    [[nodiscard]] std::uint32_t registerMesh(std::span<const SceneVertex> vertices, std::span<const std::uint32_t> indices);
    [[nodiscard]] std::uint32_t registerDatMesh(const cg::DatMeshData &datMesh);
    std::uint32_t addObject(std::uint32_t meshId, std::uint32_t materialId, const glm::mat4 &model, std::uint32_t flags = kObjectVisibleFlag);

    /// Finalizes prefix-sum buckets, uploads GPU buffers, and releases CPU vertex/index buffers.
    void buildGpuBuffers();

    /// Updates a dynamic object's model matrix / world bounds and syncs it to the GPU SSBO.
    void updateObjectTransform(std::uint32_t objectId, const glm::mat4 &model);

    /// Toggles GPU culling visibility bit without shifting SSBO indices.
    void setObjectVisible(std::uint32_t objectId, bool visible);

    /// Resets per-mesh indirect instanceCount to 0 via GPU copy from the static command template.
    void resetIndirectCommands() const;

    void bindMegaVao() const noexcept;
    void bindSceneBuffers(GLuint objectsSlot, GLuint meshesSlot, GLuint cmdsSlot, GLuint visibleIndicesSlot) const noexcept;
    void bindDrawIndirectBuffer() const noexcept;

    [[nodiscard]] std::uint32_t meshCount() const noexcept { return static_cast<std::uint32_t>(m_meshes.size()); }
    [[nodiscard]] std::uint32_t objectCount() const noexcept { return static_cast<std::uint32_t>(m_objects.size()); }

private:
    void destroyVao() noexcept;

    std::vector<SceneVertex> m_cpuVertices;
    std::vector<std::uint32_t> m_cpuIndices;
    std::vector<MeshSpan> m_meshes;
    std::vector<std::uint32_t> m_meshInstanceCounts;
    std::vector<DrawObjectData> m_objects;

    GLuint m_vao = 0;
    GpuBuffer m_vbo;
    GpuBuffer m_ibo;
    GpuBuffer m_meshSpansSSBO;
    GpuBuffer m_objectsSSBO;
    GpuBuffer m_cmdTemplateSSBO;
    GpuBuffer m_workingCmdsSSBO;
    GpuBuffer m_visibleIndicesSSBO;
};

/// Helper procedural meshes for blockout & dungeon details.
[[nodiscard]] std::uint32_t registerUnitBoxMesh(MegaScene &scene, float halfExtent = 0.5f, float uvScale = 1.0f);
[[nodiscard]] std::uint32_t registerBeveledBoxMesh(MegaScene &scene, float halfExtent = 0.5f, float bevel = 0.04f);
[[nodiscard]] std::uint32_t registerPlaneMesh(MegaScene &scene, float halfExtent = 10.0f, float uvRepeat = 8.0f);
[[nodiscard]] std::uint32_t registerRockMesh(MegaScene &scene, std::uint32_t seed);

} // namespace game::render
