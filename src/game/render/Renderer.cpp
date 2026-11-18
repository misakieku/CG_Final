#include "game/render/Renderer.hpp"

#include "game/render/passes/CompositePass.hpp"
#include "game/render/passes/CullingPass.hpp"
#include "game/render/passes/DepthPrePass.hpp"
#include "game/render/passes/GBufferPass.hpp"
#include "game/render/passes/LightCullingPass.hpp"
#include "game/render/passes/PostProcessPass.hpp"
#include "game/render/passes/ShadowPass.hpp"
#include "game/render/passes/WaterPass.hpp"

#include <glad/gl.h>

#include <iostream>
#include <utility>

namespace game::render
{

Renderer::Renderer(int initialWidth, int initialHeight, std::filesystem::path shaderDir)
    : m_shaderDir(std::move(shaderDir))
{
    // Match D3D12/Vulkan [0, 1] NDC depth for Reversed-Z precision
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);

    m_resources.onResize(initialWidth, initialHeight);
    m_resources.initBakedMaterials(m_shaderDir);

    addPass<CullingPass>(m_shaderDir, true);
    addPass<ShadowPass>(m_shaderDir);
    addPass<CullingPass>(m_shaderDir, false);
    addPass<DepthPrePass>(m_shaderDir);
    addPass<GBufferPass>(m_shaderDir);
    addPass<LightCullingPass>(m_shaderDir);
    addPass<CompositePass>(m_shaderDir);
    addPass<WaterPass>(m_shaderDir);
    addPass<PostProcessPass>(m_shaderDir);
}

bool Renderer::reloadShaders()
{
    bool allOk = true;
    try
    {
        m_resources.initBakedMaterials(m_shaderDir);
    }
    catch (const std::exception &ex)
    {
        allOk = false;
        std::cerr << "[Material Bake Reload Failed] " << ex.what() << '\n';
    }
    for (const auto &pass : m_passes)
    {
        try
        {
            pass->reloadShaders(m_shaderDir);
        }
        catch (const std::exception &ex)
        {
            allOk = false;
            std::cerr << "[Shader Reload Failed] " << ex.what() << '\n';
        }
    }
    if (allOk)
    {
        std::cout << "[Shaders] Reloaded all passes from " << m_shaderDir.generic_string() << '\n';
    }
    return allOk;
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
