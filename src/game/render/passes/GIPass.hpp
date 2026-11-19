#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

/// Sparse GPU-driven 3D L1 Spherical Harmonics probe GI:
/// Marks visible probes from m_depthTex, compacts active IDs, and dispatches Grid-DDA raytracing + temporal multi-bounce.
class GIPass final : public IRenderPass
{
public:
    explicit GIPass(const std::filesystem::path &shaderDir);
    void reloadShaders(const std::filesystem::path &shaderDir) override;
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_markShader;
    gl::Shader m_compactShader;
    gl::Shader m_traceShader;
    gl::Shader m_blendShader;
    gl::Shader m_screenShader;
};

} // namespace game::render
