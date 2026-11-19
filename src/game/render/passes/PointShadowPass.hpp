#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

/// Renders cached 6-face omnidirectional depth cubemaps for the nearest torch point lights.
class PointShadowPass final : public IRenderPass
{
public:
    explicit PointShadowPass(const std::filesystem::path &shaderDir);
    void reloadShaders(const std::filesystem::path &shaderDir) override;
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_cullShader;
    gl::Shader m_pointShadowShader;
};

} // namespace game::render
