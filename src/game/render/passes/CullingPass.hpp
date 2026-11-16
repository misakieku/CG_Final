#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

/// Compute pass performing frustum culling and bucketed MultiDrawIndirect compaction.
class CullingPass final : public IRenderPass
{
public:
    CullingPass();
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_cullShader;
};

} // namespace game::render
