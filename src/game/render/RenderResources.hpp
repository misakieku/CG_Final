#pragma once

#include "common/gl_raii.hpp"
#include "game/render/GpuBuffer.hpp"
#include "game/render/MegaScene.hpp"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>

namespace game::render
{

inline constexpr std::uint32_t kClusterCountX = 16;
inline constexpr std::uint32_t kClusterCountY = 9;
inline constexpr std::uint32_t kClusterCountZ = 24;
inline constexpr std::uint32_t kTotalClusters = kClusterCountX * kClusterCountY * kClusterCountZ;
inline constexpr std::uint32_t kMaxLightsPerCluster = 64;

/// std430-compatible point light stored in PointLightsSSBO (binding = 4).
struct alignas(16) GpuPointLight
{
    glm::vec4 positionRadius{0.0f}; // xyz = world position, w = radius
    glm::vec4 colorIntensity{1.0f}; // rgb = linear color, w = intensity
};

/// Per-frame CPU inputs passed into Renderer::renderFrame().
struct RenderContext
{
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::vec3 cameraPos{0.0f};
    float nearPlane = 0.1f;
    float farPlane = 150.0f;
    float elapsedTime = 0.0f;
    float deltaTime = 0.0f;
    std::uint32_t frameIndex = 0;
    int viewportWidth = 1280;
    int viewportHeight = 720;
};

/// std140 UBO bound at binding = 0 across culling, G-buffer, light culling, and lighting passes.
struct alignas(16) FrameUniforms
{
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::mat4 invProj{1.0f};
    glm::mat4 viewProj{1.0f};
    glm::mat4 invViewProj{1.0f};
    glm::vec4 cameraPos{0.0f};
    std::array<glm::vec4, 6> frustumPlanes{};
    glm::vec4 screenParams{0.0f}; // x=w, y=h, z=1/w, w=1/h
    glm::vec4 timeParams{0.0f};   // x=time, y=dt, z=near, w=far
    glm::uvec4 counts{0u};        // x=objectCount, y=meshCount, z=lightCount, w=frameIndex
    glm::uvec4 clusterDims{kClusterCountX, kClusterCountY, kClusterCountZ, kMaxLightsPerCluster};
    glm::mat4 lightViewProj{1.0f};
    std::array<glm::vec4, 6> shadowFrustumPlanes{};
    glm::vec4 shadowParams{0.0f}; // x=invShadowSize, y=halfExtent, z=enabled, w=unused
    glm::vec4 giGridOrigin{0.0f}; // xyz=probe(0,0,0) world pos, w=probeStepXZ
    glm::vec4 giGridInvStep{0.0f}; // xyz=1/probeStep, w=cellSize
    glm::uvec4 giDims{0u};        // x=nx, y=ny, z=nz, w=totalProbes
    glm::vec4 giMisc{0.0f};       // x=wallHeight, y=gridW, z=gridH, w=whiteboxAlbedo (<=0 uses albedoArray)
};

/// Shared GPU render targets, FBO bundles, scene buffers, clustered light buffers, and material array owned by Renderer.
class RenderResources
{
public:
    RenderResources();
    ~RenderResources();

    RenderResources(const RenderResources &) = delete;
    RenderResources &operator=(const RenderResources &) = delete;

    void onResize(int width, int height);
    void initBakedMaterials(const std::filesystem::path &shaderDir);
    void initGiGrid(glm::ivec2 gridSize, float cellSize, float wallHeight, std::span<const std::uint8_t> cellKinds);
    void setPointLights(std::span<const GpuPointLight> lights);
    void updatePointLight(std::uint32_t index, const GpuPointLight &light);
    void updateFrameUniforms(const RenderContext &ctx);
    void drawFullscreenTriangle() const noexcept;

    [[nodiscard]] MegaScene &scene() noexcept { return m_scene; }
    [[nodiscard]] const MegaScene &scene() const noexcept { return m_scene; }

    [[nodiscard]] const GpuBuffer &frameUbo() const noexcept { return m_frameUbo; }
    [[nodiscard]] const GpuBuffer &lightsSSBO() const noexcept { return m_lightsSSBO; }
    [[nodiscard]] const GpuBuffer &clusterCountsSSBO() const noexcept { return m_clusterCountsSSBO; }
    [[nodiscard]] const GpuBuffer &clusterIndicesSSBO() const noexcept { return m_clusterIndicesSSBO; }
    [[nodiscard]] std::uint32_t lightCount() const noexcept { return m_lightCount; }

    static constexpr int kShadowMapSize = 2048;
    static constexpr float kShadowHalfExtent = 26.0f;

    [[nodiscard]] const gl::Texture &gbuffer0() const noexcept { return m_gbuffer0; }
    [[nodiscard]] const gl::Texture &gbuffer1() const noexcept { return m_gbuffer1; }
    [[nodiscard]] const gl::Texture &depthTex() const noexcept { return m_depthTex; }
    [[nodiscard]] const gl::Texture &hdrTex() const noexcept { return m_hdrTex; }
    [[nodiscard]] const gl::Texture &shadowDepthTex() const noexcept { return m_shadowDepthTex; }

    [[nodiscard]] const gl::Framebuffer &gbufferFbo() const noexcept { return m_gbufferFbo; }
    [[nodiscard]] const gl::Framebuffer &puddleFbo() const noexcept { return m_puddleFbo; }
    [[nodiscard]] const gl::Framebuffer &hdrFbo() const noexcept { return m_hdrFbo; }
    [[nodiscard]] const gl::Framebuffer &shadowFbo() const noexcept { return m_shadowFbo; }

    [[nodiscard]] GLuint mazeGridTexId() const noexcept { return m_mazeGridTex; }
    [[nodiscard]] GLuint giShTexId(int idx) const noexcept { return m_giShTex[static_cast<std::size_t>(idx)]; }
    [[nodiscard]] const GpuBuffer &giProbeMetaSSBO() const noexcept { return m_giProbeMetaSSBO; }
    [[nodiscard]] const GpuBuffer &giActiveFlagSSBO() const noexcept { return m_giActiveFlagSSBO; }
    [[nodiscard]] const GpuBuffer &giActiveListSSBO() const noexcept { return m_giActiveListSSBO; }
    [[nodiscard]] const GpuBuffer &giDispatchCmdSSBO() const noexcept { return m_giDispatchCmdSSBO; }
    [[nodiscard]] const GpuBuffer &giStagingShSSBO() const noexcept { return m_giStagingShSSBO; }
    [[nodiscard]] const GpuBuffer &giOctCacheSSBO() const noexcept { return m_giOctCacheSSBO; }
    [[nodiscard]] glm::uvec3 giProbeDims() const noexcept { return m_giProbeDims; }
    [[nodiscard]] bool hasGiGrid() const noexcept { return m_mazeGridTex != 0 && m_giProbeDims.x > 0; }

    [[nodiscard]] GLuint albedoArrayTexId() const noexcept { return m_albedoArrayTex; }
    [[nodiscard]] GLuint ormArrayTexId() const noexcept { return m_ormArrayTex; }
    [[nodiscard]] GLuint normalArrayTexId() const noexcept { return m_normalArrayTex; }
    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }

private:
    void createStarterMaterialArrays();

    static constexpr int kMaterialArraySize = 1024;
    static constexpr int kMaterialArrayLayers = 9;
    static constexpr int kMaterialMipLevels = 11;

    int m_width = 0;
    int m_height = 0;

    MegaScene m_scene;
    GpuBuffer m_frameUbo;

    GpuBuffer m_lightsSSBO;
    GpuBuffer m_clusterCountsSSBO;
    GpuBuffer m_clusterIndicesSSBO;
    std::uint32_t m_lightCount = 0;

    gl::Texture m_gbuffer0; // RGBA8: Albedo.rgb + AO
    gl::Texture m_gbuffer1; // RGBA16F: WorldNormal.xyz + Roughness
    gl::Texture m_depthTex; // DEPTH_COMPONENT32F (Reversed-Z [0, 1])
    gl::Texture m_hdrTex;   // RGBA16F: Lit HDR accumulation
    gl::Texture m_shadowDepthTex; // DEPTH_COMPONENT32F ([0, 1] linear directional shadow map)

    gl::Framebuffer m_gbufferFbo;
    gl::Framebuffer m_puddleFbo; // Color attachments only (no depth attached so depthTex can be sampled safely)
    gl::Framebuffer m_hdrFbo;
    gl::Framebuffer m_shadowFbo;

    GLuint m_albedoArrayTex = 0;
    GLuint m_ormArrayTex = 0;
    GLuint m_normalArrayTex = 0;
    GLuint m_emptyVao = 0;

    GLuint m_mazeGridTex = 0;
    std::array<GLuint, 3> m_giShTex{0, 0, 0};
    GpuBuffer m_giProbeMetaSSBO;
    GpuBuffer m_giActiveFlagSSBO;
    GpuBuffer m_giActiveListSSBO;
    GpuBuffer m_giDispatchCmdSSBO;
    GpuBuffer m_giStagingShSSBO;
    GpuBuffer m_giOctCacheSSBO;
    glm::ivec2 m_giMapSize{0};
    glm::uvec3 m_giProbeDims{0u};
    float m_giCellSize = 3.0f;
    float m_giWallHeight = 3.5f;
};

} // namespace game::render
