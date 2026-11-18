#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

namespace game::render
{

/// Compute pass performing frustum culling and bucketed MultiDrawIndirect compaction.
class CullingPass final : public IRenderPass
{
public:
    explicit CullingPass(const std::filesystem::path &shaderDir, bool cullShadow = false);
    void reloadShaders(const std::filesystem::path &shaderDir) override;
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_cullShader;
    bool m_cullShadow = false;
};

} // namespace game::render
