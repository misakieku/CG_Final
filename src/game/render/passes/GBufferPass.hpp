#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

/// Geometry pass drawing all culled scene instances in one bucketed MultiDrawElementsIndirect call.
class GBufferPass final : public IRenderPass
{
public:
    GBufferPass();
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_gbufferShader;
};

} // namespace game::render
