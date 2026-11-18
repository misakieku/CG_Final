#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

/// Half-resolution tangent-hemisphere SSAO + cross-bilateral depth-aware blur for local contact occlusion.
class SSAOPass final : public IRenderPass
{
public:
    explicit SSAOPass(const std::filesystem::path &shaderDir);
    void reloadShaders(const std::filesystem::path &shaderDir) override;
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_ssaoShader;
    gl::Shader m_blurShader;
};

} // namespace game::render
