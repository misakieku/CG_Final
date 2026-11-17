#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

/// Transparent forward water pass rendering depth-faded, ripple-perturbed, Fresnel-lit oasis ponds into HDR buffer.
class WaterPass final : public IRenderPass
{
public:
    explicit WaterPass(const std::filesystem::path &shaderDir);
    void reloadShaders(const std::filesystem::path &shaderDir) override;
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_waterShader;
};

} // namespace game::render
