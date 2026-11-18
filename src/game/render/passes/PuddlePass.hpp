#pragma once

#include "common/gl_raii.hpp"
#include "game/render/IRenderPass.hpp"

#include <filesystem>

namespace game::render
{

/// Deferred G-buffer modifier pass applying puddles and wet mud rims exclusively onto kMatFloor pixels.
class PuddlePass final : public IRenderPass
{
public:
    explicit PuddlePass(const std::filesystem::path &shaderDir);

    void reloadShaders(const std::filesystem::path &shaderDir) override;
    void execute(const RenderContext &ctx, RenderResources &res) override;

private:
    gl::Shader m_puddleShader;
};

} // namespace game::render
