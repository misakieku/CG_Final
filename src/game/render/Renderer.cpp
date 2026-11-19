#include "game/render/Renderer.hpp"

#include "game/render/passes/BloomPass.hpp"
#include "game/render/passes/CompositePass.hpp"
#include "game/render/passes/CullingPass.hpp"
#include "game/render/passes/DepthPrePass.hpp"
#include "game/render/passes/FlamePass.hpp"
#include "game/render/passes/FxaaPass.hpp"
#include "game/render/passes/GBufferPass.hpp"
#include "game/render/passes/GIPass.hpp"
#include "game/render/passes/HzbPass.hpp"
#include "game/render/passes/LightCullingPass.hpp"
#include "game/render/passes/PointShadowPass.hpp"
#include "game/render/passes/PostProcessPass.hpp"
#include "game/render/passes/ProbeDebugPass.hpp"
#include "game/render/passes/PuddlePass.hpp"
#include "game/render/passes/SSAOPass.hpp"
#include "game/render/passes/SSPRPass.hpp"
#include "game/render/passes/ShadowPass.hpp"
#include "game/render/passes/VolumetricFogPass.hpp"
#include "game/render/passes/WaterPass.hpp"

#include <glad/gl.h>

#include <iostream>
#include <utility>

namespace game::render
{

Renderer::Renderer(int initialWidth, int initialHeight, std::filesystem::path shaderDir, bool deferInit)
    : m_shaderDir(std::move(shaderDir))
    , m_resources(deferInit)
{
    // Match D3D12/Vulkan [0, 1] NDC depth for Reversed-Z precision
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);

    m_resources.onResize(initialWidth, initialHeight);
    if (!deferInit)
    {
        m_resources.initBakedMaterials(m_shaderDir);
        std::string unused;
        while (stepNextPassInit(unused))
        {
        }
    }
}

void Renderer::initMaterialsFromCpu(const StarterMaterialsCpuData &cpuData)
{
    m_resources.uploadStarterMaterialArrays(cpuData);
    m_resources.initBakedMaterials(m_shaderDir);
}

bool Renderer::stepNextPassInit(std::string &outPassName)
{
    const int step = m_initPassCursor++;
    auto reg = [&]<typename PassT, typename... Args>(std::string_view name, Args &&...args)
    {
        outPassName = std::string(name);
        addPass<PassT>(name, std::forward<Args>(args)...);
    };

    switch (step)
    {
    case 0: reg.template operator()<CullingPass>("ShadowCull", m_shaderDir, CullingStage::Shadow); break;
    case 1: reg.template operator()<ShadowPass>("ShadowMap", m_shaderDir); break;
    case 2: reg.template operator()<PointShadowPass>("PointShadow", m_shaderDir); break;
    case 3: reg.template operator()<CullingPass>("MainCull1", m_shaderDir, CullingStage::MainPass1); break;
    case 4: reg.template operator()<DepthPrePass>("DepthPass1", m_shaderDir, DepthStage::Pass1Opaque); break;
    case 5: reg.template operator()<HzbPass>("HzbBuild1", m_shaderDir, false); break;
    case 6: reg.template operator()<CullingPass>("MainCull2", m_shaderDir, CullingStage::MainPass2); break;
    case 7: reg.template operator()<DepthPrePass>("DepthRescue", m_shaderDir, DepthStage::Pass2RescueOpaque); break;
    case 8: reg.template operator()<HzbPass>("HzbBuild2", m_shaderDir, true); break;
    case 9: reg.template operator()<DepthPrePass>("DepthGrass", m_shaderDir, DepthStage::Grass); break;
    case 10: reg.template operator()<GBufferPass>("GBuffer", m_shaderDir); break;
    case 11: reg.template operator()<PuddlePass>("Puddles", m_shaderDir); break;
    case 12: reg.template operator()<LightCullingPass>("LightCull", m_shaderDir); break;
    case 13: reg.template operator()<SSAOPass>("SSAO", m_shaderDir); break;
    case 14: reg.template operator()<GIPass>("ProbeGI", m_shaderDir); break;
    case 15: reg.template operator()<CompositePass>("Composite", m_shaderDir); break;
    case 16: reg.template operator()<FlamePass>("Flames", m_shaderDir); break;
    case 17: reg.template operator()<SSPRPass>("SSPR", m_shaderDir); break;
    case 18: reg.template operator()<ProbeDebugPass>("ProbeDebug", m_shaderDir); break;
    case 19: reg.template operator()<VolumetricFogPass>("VolFog", m_shaderDir); break;
    case 20: reg.template operator()<BloomPass>("Bloom", m_shaderDir); break;
    case 21: reg.template operator()<PostProcessPass>("PostProcess", m_shaderDir); break;
    case 22: reg.template operator()<FxaaPass>("FXAA", m_shaderDir); break;
    default: return false;
    }
    return m_initPassCursor < totalPassInitSteps();
}

Renderer::~Renderer()
{
    destroyQueryPool();
}

void Renderer::destroyQueryPool() noexcept
{
    for (auto &pool : m_queryPools)
    {
        if (!pool.empty())
        {
            glDeleteQueries(static_cast<GLsizei>(pool.size()), pool.data());
            pool.clear();
        }
    }
    m_queryValid.fill(false);
}

void Renderer::ensureQueryPool()
{
    const std::size_t needed = m_passes.size() + 1u;
    if (!m_queryPools[0].empty() && m_queryPools[0].size() == needed)
    {
        return;
    }
    destroyQueryPool();
    for (auto &pool : m_queryPools)
    {
        pool.resize(needed, 0u);
        glGenQueries(static_cast<GLsizei>(needed), pool.data());
    }
}

void Renderer::tryCollectOldestTimings()
{
    const std::size_t readSlot = (m_queryWriteSlot + 1u) % kQueryRing;
    if (!m_queryValid[readSlot] || m_queryPools[readSlot].empty())
    {
        return;
    }

    GLuint available = 0;
    glGetQueryObjectuiv(m_queryPools[readSlot].back(), GL_QUERY_RESULT_AVAILABLE, &available);
    if (available == 0)
    {
        return;
    }

    const auto &pool = m_queryPools[readSlot];
    GLuint64 prevTs = 0;
    glGetQueryObjectui64v(pool[0], GL_QUERY_RESULT, &prevTs);
    float totalMs = 0.0f;
    for (std::size_t i = 0; i < m_passes.size(); ++i)
    {
        GLuint64 nextTs = 0;
        glGetQueryObjectui64v(pool[i + 1u], GL_QUERY_RESULT, &nextTs);
        const float ms = (nextTs >= prevTs) ? static_cast<float>(nextTs - prevTs) * 1e-6f : 0.0f;
        m_passTimings[i].gpuMs = (m_passTimings[i].gpuMs > 0.0f) ? (m_passTimings[i].gpuMs * 0.85f + ms * 0.15f) : ms;
        totalMs += m_passTimings[i].gpuMs;
        prevTs = nextTs;
    }
    m_totalGpuMs = totalMs;
    m_queryValid[readSlot] = false;
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
    for (const auto &entry : m_passes)
    {
        try
        {
            entry.pass->reloadShaders(m_shaderDir);
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
    for (const auto &entry : m_passes)
    {
        entry.pass->onResize(width, height, m_resources);
    }
}

void Renderer::renderFrame(const RenderContext &ctx)
{
    m_resources.updateFrameUniforms(ctx);

    if (ctx.profilePasses)
    {
        ensureQueryPool();
        tryCollectOldestTimings();
        const auto &pool = m_queryPools[m_queryWriteSlot];
        glQueryCounter(pool[0], GL_TIMESTAMP);
        for (std::size_t i = 0; i < m_passes.size(); ++i)
        {
            m_passes[i].pass->execute(ctx, m_resources);
            glQueryCounter(pool[i + 1u], GL_TIMESTAMP);
        }
        m_queryValid[m_queryWriteSlot] = true;
        m_queryWriteSlot = (m_queryWriteSlot + 1u) % kQueryRing;
    }
    else
    {
        for (const auto &entry : m_passes)
        {
            entry.pass->execute(ctx, m_resources);
        }
    }

    m_resources.advanceHzbHistory();
    m_resources.markFrameSubmitted();
}

} // namespace game::render
