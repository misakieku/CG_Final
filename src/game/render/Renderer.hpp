#pragma once

#include "game/render/IRenderPass.hpp"
#include "game/render/RenderResources.hpp"

#include <glad/gl.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace game::render
{

struct PassTiming
{
    std::string name;
    float gpuMs = 0.0f;
};

/// Coordinates OpenGL 4.6 global clip state, shared RenderResources, sequential IRenderPass execution, and optional per-pass GPU timing.
class Renderer
{
public:
    Renderer(int initialWidth, int initialHeight, std::filesystem::path shaderDir, bool deferInit = false);
    ~Renderer();

    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    [[nodiscard]] int totalPassInitSteps() const noexcept { return 23; }
    [[nodiscard]] int completedPassInitSteps() const noexcept { return m_initPassCursor; }
    bool stepNextPassInit(std::string &outPassName);
    void initMaterialsFromCpu(const StarterMaterialsCpuData &cpuData);

    template <typename PassT, typename... Args>
    PassT &addPass(std::string_view name, Args &&...args)
    {
        auto pass = std::make_unique<PassT>(std::forward<Args>(args)...);
        PassT &ref = *pass;
        m_passes.push_back({std::string(name), std::move(pass)});
        m_passTimings.push_back({std::string(name), 0.0f});
        return ref;
    }

    void onResize(int width, int height);
    void renderFrame(const RenderContext &ctx);
    bool reloadShaders();

    [[nodiscard]] RenderResources &resources() noexcept { return m_resources; }
    [[nodiscard]] const RenderResources &resources() const noexcept { return m_resources; }
    [[nodiscard]] const std::vector<PassTiming> &passTimings() const noexcept { return m_passTimings; }
    [[nodiscard]] float totalGpuMs() const noexcept { return m_totalGpuMs; }

private:
    struct PassEntry
    {
        std::string name;
        std::unique_ptr<IRenderPass> pass;
    };

    void ensureQueryPool();
    void tryCollectOldestTimings();
    void destroyQueryPool() noexcept;

    static constexpr std::size_t kQueryRing = 4;

    std::filesystem::path m_shaderDir;
    RenderResources m_resources;
    std::vector<PassEntry> m_passes;
    std::vector<PassTiming> m_passTimings;
    float m_totalGpuMs = 0.0f;
    int m_initPassCursor = 0;

    std::array<std::vector<GLuint>, kQueryRing> m_queryPools;
    std::array<bool, kQueryRing> m_queryValid{};
    std::size_t m_queryWriteSlot = 0;
};

} // namespace game::render
