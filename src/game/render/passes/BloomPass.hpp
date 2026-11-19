#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

/// Half-res 6-mip HDR bloom pyramid using Karis prefilter, Jimenez 13-tap downsample, and 9-tap tent upsample.
class BloomPass final : public IRenderPass
{
public:
    explicit BloomPass(const std::filesystem::path &shaderDir);
    void reloadShaders(const std::filesystem::path &shaderDir) override;
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_prefilterShader;
    gl::Shader m_downsampleShader;
    gl::Shader m_upsampleShader;
};

} // namespace game::render
