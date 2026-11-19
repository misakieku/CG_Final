#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

enum class CullingStage
{
    Shadow,
    MainPass1,
    MainPass2,
};

/// Compute pass performing frustum, screen-percentage, and two-pass HZB occlusion culling.
class CullingPass final : public IRenderPass
{
public:
    explicit CullingPass(const std::filesystem::path &shaderDir, CullingStage stage = CullingStage::MainPass1);
    void reloadShaders(const std::filesystem::path &shaderDir) override;
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_cullShader;
    gl::Shader m_grassCullShader;
    CullingStage m_stage = CullingStage::MainPass1;
};

} // namespace game::render
