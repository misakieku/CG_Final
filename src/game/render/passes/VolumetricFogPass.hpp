#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

#include <filesystem>

namespace game::render
{

/// Two-stage 3D froxel volumetric fog pass reusing clustered point lights, directional shadow, and GI probes.
class VolumetricFogPass final : public IRenderPass
{
public:
    explicit VolumetricFogPass(const std::filesystem::path &shaderDir);

    void reloadShaders(const std::filesystem::path &shaderDir) override;
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_injectShader;
    gl::Shader m_accumulateShader;
};

} // namespace game::render
