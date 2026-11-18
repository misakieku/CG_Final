#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

/// Renders a single orthographic directional shadow map from the culled shadow frustum.
class ShadowPass final : public IRenderPass
{
public:
    explicit ShadowPass(const std::filesystem::path &shaderDir);
    void reloadShaders(const std::filesystem::path &shaderDir) override;
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_shadowShader;
};

} // namespace game::render
