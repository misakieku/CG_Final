#pragma once

#include "common/gl_raii.hpp"
#include "game/render/GpuBuffer.hpp"
#include "game/render/IRenderPass.hpp"

#include <glad/gl.h>

#include <cstdint>

namespace game::render
{

/// Renders floating L1 Spherical Harmonics irradiance probe spheres inside m_hdrDepthFbo for dynamic GI debugging.
class ProbeDebugPass final : public IRenderPass
{
public:
    explicit ProbeDebugPass(const std::filesystem::path &shaderDir);
    ~ProbeDebugPass() override;

    ProbeDebugPass(const ProbeDebugPass &) = delete;
    ProbeDebugPass &operator=(const ProbeDebugPass &) = delete;

    void reloadShaders(const std::filesystem::path &shaderDir) override;
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    void buildSphereMesh();

    gl::Shader m_probeShader;
    GLuint m_vao = 0;
    GpuBuffer m_vbo;
    GpuBuffer m_ibo;
    std::uint32_t m_indexCount = 0;
};

} // namespace game::render
