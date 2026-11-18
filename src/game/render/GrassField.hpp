#pragma once

#include "game/render/GpuBuffer.hpp"
#include "game/render/MegaScene.hpp"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <span>

namespace game::render
{

/// std430-compatible 32-byte per-tuft GPU grass instance record.
struct alignas(16) GrassInstance
{
    glm::vec4 posYaw{0.0f};    // xyz = world root, w = azimuth yaw (rad)
    glm::vec4 scaleSeed{1.0f}; // x = widthScale, y = heightScale, z = colorSeed [0,1], w = lean
};

/// Persistent GPU buffers and stylized 4-blade clump mesh for GPU-culled solid grass rendering.
class GrassField
{
public:
    GrassField() = default;
    ~GrassField();

    GrassField(const GrassField &) = delete;
    GrassField &operator=(const GrassField &) = delete;
    GrassField(GrassField &&other) noexcept;
    GrassField &operator=(GrassField &&other) noexcept;

    void uploadInstances(std::span<const GrassInstance> instances);
    void clear() noexcept;

    void resetIndirectCommand() const;
    void bindForCull(GLuint instancesSlot, GLuint cmdsSlot, GLuint visibleIndicesSlot) const noexcept;
    void drawIndirect(GLuint instancesSlot, GLuint visibleIndicesSlot) const noexcept;

    [[nodiscard]] std::uint32_t instanceCount() const noexcept { return m_instanceCount; }
    [[nodiscard]] std::uint32_t indexCount() const noexcept { return m_indexCount; }

private:
    void buildClumpGeometryIfNeeded();
    void destroyVao() noexcept;

    std::uint32_t m_instanceCount = 0;
    std::uint32_t m_indexCount = 0;

    GLuint m_vao = 0;
    GpuBuffer m_vbo;
    GpuBuffer m_ibo;
    GpuBuffer m_instancesSSBO;
    GpuBuffer m_visibleIndicesSSBO;
    GpuBuffer m_cmdTemplateSSBO;
    GpuBuffer m_workingCmdsSSBO;
};

} // namespace game::render
