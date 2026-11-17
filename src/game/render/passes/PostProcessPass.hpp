#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

/// Final fullscreen post-process pass applying exponential depth fog, ACES filmic tonemapping, and gamma 2.2 to default FBO.
class PostProcessPass final : public IRenderPass
{
public:
    PostProcessPass();
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_postShader;
};

} // namespace game::render
