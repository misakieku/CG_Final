#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

/// Fullscreen clustered deferred lighting pass outputting linear HDR into RenderResources::hdrFbo().
class CompositePass final : public IRenderPass
{
public:
    CompositePass();
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_compositeShader;
};

} // namespace game::render
