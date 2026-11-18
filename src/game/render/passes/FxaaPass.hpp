#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

/// Final screen-space FXAA 3.11 pass reading sRGB + luma from m_ldrTex and presenting to the default framebuffer.
class FxaaPass final : public IRenderPass
{
public:
    explicit FxaaPass(const std::filesystem::path &shaderDir);
    void reloadShaders(const std::filesystem::path &shaderDir) override;
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_fxaaShader;
};

} // namespace game::render
