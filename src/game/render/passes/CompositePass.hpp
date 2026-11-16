#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

/// Starter fullscreen lighting/present pass reading G-Buffer + Reversed-Z depth to the default FBO.
class CompositePass final : public IRenderPass
{
public:
    CompositePass();
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_compositeShader;
};

} // namespace game::render
