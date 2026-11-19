#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

/// Compute pass building a conservative Reversed-Z min-depth HZB mip chain into RenderResources::currHzbTexId().
class HzbPass final : public IRenderPass
{
public:
    explicit HzbPass(const std::filesystem::path &shaderDir, bool markHistoryValid = false);
    void reloadShaders(const std::filesystem::path &shaderDir) override;
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_buildShader;
    bool m_markHistoryValid = false;
};

} // namespace game::render
