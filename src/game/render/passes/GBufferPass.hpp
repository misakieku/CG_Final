#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

/// Geometry pass drawing all culled scene instances in one bucketed MultiDrawElementsIndirect call.
class GBufferPass final : public IRenderPass
{
public:
    explicit GBufferPass(const std::filesystem::path &shaderDir);
    void reloadShaders(const std::filesystem::path &shaderDir) override;
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_gbufferShader;
    gl::Shader m_grassGbufferShader;
};

} // namespace game::render
