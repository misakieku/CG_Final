#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{
class LightCullingPass : public IRenderPass
{
public:
    LightCullingPass();
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_cullingShader;
};
} // namespace game::render
