#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

/// Three-stage compute Screen-Space Planar Reflection (SSPR) across the y = 0 puddle plane.
class SSPRPass final : public IRenderPass
{
public:
    explicit SSPRPass(const std::filesystem::path &shaderDir);
    void reloadShaders(const std::filesystem::path &shaderDir) override;
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_projectShader;
    gl::Shader m_resolveShader;
    gl::Shader m_applyShader;
};

} // namespace game::render
