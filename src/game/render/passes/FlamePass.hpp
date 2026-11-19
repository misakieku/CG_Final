#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

/// Renders procedural animated HDR flame billboards for torches and candles into m_hdrTex.
class FlamePass final : public IRenderPass
{
public:
    explicit FlamePass(const std::filesystem::path &shaderDir);
    void reloadShaders(const std::filesystem::path &shaderDir) override;
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_flameShader;
};

} // namespace game::render
