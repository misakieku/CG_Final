#pragma once

#include "game/render/RenderResources.hpp"

#include <filesystem>

namespace game::render
{

/// Interface for modular OpenGL render passes executed sequentially by Renderer.
class IRenderPass
{
public:
    virtual ~IRenderPass() = default;

    virtual void reloadShaders(const std::filesystem::path &shaderDir)
    {
        (void)shaderDir;
    }

    virtual void onResize(int width, int height, RenderResources &res)
    {
        (void)width;
        (void)height;
        (void)res;
    }

    virtual void execute(const RenderContext &ctx, RenderResources &res) = 0;
};

} // namespace game::render
