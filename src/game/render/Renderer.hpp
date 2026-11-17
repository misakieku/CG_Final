#pragma once

#include "game/render/IRenderPass.hpp"
#include "game/render/RenderResources.hpp"

#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

namespace game::render
{

/// Coordinates OpenGL 4.6 global clip state, shared RenderResources, and sequential IRenderPass execution.
class Renderer
{
public:
    Renderer(int initialWidth, int initialHeight, std::filesystem::path shaderDir);

    template <typename PassT, typename... Args>
    PassT &addPass(Args &&...args)
    {
        auto pass = std::make_unique<PassT>(std::forward<Args>(args)...);
        PassT &ref = *pass;
        m_passes.push_back(std::move(pass));
        return ref;
    }

    void onResize(int width, int height);
    void renderFrame(const RenderContext &ctx);
    bool reloadShaders();

    [[nodiscard]] RenderResources &resources() noexcept { return m_resources; }
    [[nodiscard]] const RenderResources &resources() const noexcept { return m_resources; }

private:
    std::filesystem::path m_shaderDir;
    RenderResources m_resources;
    std::vector<std::unique_ptr<IRenderPass>> m_passes;
};

} // namespace game::render
