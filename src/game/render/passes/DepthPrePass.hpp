#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

/// Z-prepass populating Reversed-Z m_depthTex with zero color writes so GBufferPass runs strictly 1x per visible pixel.
class DepthPrePass final : public IRenderPass
{
public:
    explicit DepthPrePass(const std::filesystem::path &shaderDir);
    void reloadShaders(const std::filesystem::path &shaderDir) override;
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_depthShader;
    gl::Shader m_grassDepthShader;
};

} // namespace game::render
