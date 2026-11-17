#include "game/render/Renderer.hpp"

#include "game/render/passes/CompositePass.hpp"
#include "game/render/passes/CullingPass.hpp"
#include "game/render/passes/GBufferPass.hpp"
#include "game/render/passes/LightCullingPass.hpp"
#include "game/render/passes/PostProcessPass.hpp"
#include "game/render/passes/WaterPass.hpp"

#include <glad/gl.h>

namespace game::render
{

Renderer::Renderer(int initialWidth, int initialHeight)
{
    // Match D3D12/Vulkan [0, 1] NDC depth for Reversed-Z precision
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);

    m_resources.onResize(initialWidth, initialHeight);

    addPass<CullingPass>();
    addPass<GBufferPass>();
    addPass<LightCullingPass>();
    addPass<CompositePass>();
    addPass<WaterPass>();
    addPass<PostProcessPass>();
}

void Renderer::onResize(int width, int height)
{
    m_resources.onResize(width, height);
    for (const auto &pass : m_passes)
    {
        pass->onResize(width, height, m_resources);
    }
}

void Renderer::renderFrame(const RenderContext &ctx)
{
    m_resources.updateFrameUniforms(ctx);
    for (const auto &pass : m_passes)
    {
        pass->execute(ctx, m_resources);
    }
}

} // namespace game::render
