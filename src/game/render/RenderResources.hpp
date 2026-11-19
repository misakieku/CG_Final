#pragma once

#include "common/gl_raii.hpp"
#include "game/render/GpuBuffer.hpp"
#include "game/render/GrassField.hpp"
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

inline constexpr std::uint32_t kFogVolumeWidth = 128;
inline constexpr std::uint32_t kFogVolumeHeight = 72;
inline constexpr std::uint32_t kFogVolumeDepth = 48;

inline constexpr std::uint32_t kFeatureGiBit = 1u << 0;
inline constexpr std::uint32_t kFeatureSsaoBit = 1u << 1;
inline constexpr std::uint32_t kFeatureFogBit = 1u << 2;
inline constexpr std::uint32_t kFeatureBloomBit = 1u << 3;
inline constexpr std::uint32_t kFeatureSsrBit = 1u << 4;
inline constexpr std::uint32_t kFeatureShadowsBit = 1u << 5;
inline constexpr std::uint32_t kFeaturePointShadowsBit = 1u << 6;
inline constexpr std::uint32_t kFeatureIndirectOnlyBit = 1u << 7;

/// std430-compatible point light stored in PointLightsSSBO (binding = 4).
struct alignas(16) GpuPointLight
{
    glm::vec4 positionRadius{0.0f}; // xyz = world position, w = radius
    glm::vec4 colorIntensity{1.0f}; // rgb = linear color, w = intensity
};

/// std430-compatible procedural flame billboard instance stored in FlamesSSBO (binding = 0 in FlamePass).
struct alignas(16) FlameInstance
{
    glm::vec4 posWidth{0.0f};   // xyz = base world pos, w = billboard width
    glm::vec4 heightSeed{1.0f}; // x = height, y = phase seed, z = hdr intensity, w = kind (0=candle, 1=torch)
};

/// Per-feature toggle flags for runtime debug panel & live demo isolation.
struct RenderFeatureFlags
{
    bool enableShadows = true;
    bool enablePointShadows = true;
    bool enableHzb = true;
    bool enablePuddles = true;
    bool enableSsao = true;
    bool enableGi = true;
    bool enableFlames = true;
    bool enableSspr = true;
    bool enableFog = true;
    bool enableBloom = true;
    bool enableFxaa = true;
    bool enableProbeDebug = false;
    bool enableIndirectOnly = false;
};

/// Precomputed CPU pixel buffers for starter material array layers 2..7 (layers 0, 1, 8 are GPU-baked).
struct StarterMaterialsCpuData
{
    struct Layer
    {
        int layerIndex = 0;
        std::vector<std::uint8_t> albedoPixels;
        std::vector<std::uint8_t> ormPixels;
        std::vector<std::uint8_t> normalPixels;
    };
    std::vector<Layer> layers;
};

/// Per-frame CPU inputs passed into Renderer::renderFrame().
struct RenderContext
{
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::vec3 cameraPos{0.0f};
    glm::vec3 moonDirection{0.43967f, 0.83049f, 0.34197f};
    float nearPlane = 0.1f;
    float farPlane = 150.0f;
    float elapsedTime = 0.0f;
    float deltaTime = 0.0f;
    std::uint32_t frameIndex = 0;
    int viewportWidth = 1280;
    int viewportHeight = 720;
    int presentWidth = 1280;
    int presentHeight = 720;
    float fogDensity = 0.024f;
    float fogAnisotropy = 0.33f;
    float fogHeightFalloff = 0.22f;
    float fogFar = 56.0f;
    float probeDebugRadius = 0.11f;
    bool whiteboxMode = false;
    bool profilePasses = false;
    RenderFeatureFlags features{};
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
    glm::vec4 shadowParams{0.0f}; // x=invShadowSize, y=halfExtent, z=dirShadowEnabled, w=pointShadowEnabled
    glm::vec4 moonDir{0.43967f, 0.83049f, 0.34197f, 0.0f};
    glm::vec4 giGridOrigin{0.0f}; // xyz=probe(0,0,0) world pos, w=probeStepXZ
    glm::vec4 giGridInvStep{0.0f}; // xyz=1/probeStep, w=cellSize
    glm::uvec4 giDims{0u};        // x=nx, y=ny, z=nz, w=totalProbes
    glm::vec4 giMisc{0.0f};       // x=wallHeight, y=gridW, z=gridH, w=whiteboxAlbedo (<=0 uses albedoArray)
    glm::mat4 prevViewProj{1.0f};
    glm::vec4 fogParams{0.024f, 0.33f, 0.22f, 56.0f}; // x=density, y=anisotropy, z=heightFalloff, w=fogFar
    glm::uvec4 fogDims{kFogVolumeWidth, kFogVolumeHeight, kFogVolumeDepth, 0u};
    glm::uvec4 hzbParams{0u}; // x=width, y=height, z=mipCount, w=prevHzbValid
    std::array<glm::uvec4, 4> pointShadowMeta{}; // x=lightIdx, y=nearBits, z=farBits, w=fadeBits
};

/// Shared GPU render targets, FBO bundles, scene buffers, clustered light buffers, and material array owned by Renderer.
class RenderResources
{
public:
    explicit RenderResources(bool deferMaterialGen = false);
    ~RenderResources();

    RenderResources(const RenderResources &) = delete;
    RenderResources &operator=(const RenderResources &) = delete;

    [[nodiscard]] static StarterMaterialsCpuData generateStarterMaterialsCpu();
    void uploadStarterMaterialArrays(const StarterMaterialsCpuData &cpuData);
    void onResize(int width, int height);
    void initBakedMaterials(const std::filesystem::path &shaderDir);
    void initGiGrid(glm::ivec2 gridSize, float cellSize, float wallHeight, std::span<const std::uint8_t> cellKinds);
    void setPointLights(std::span<const GpuPointLight> lights, std::size_t shadowCasterCount = 0);
    void setFlames(std::span<const FlameInstance> flames);
    void updatePointLight(std::uint32_t index, const GpuPointLight &light);
    void updateFrameUniforms(const RenderContext &ctx);
    void markFrameSubmitted() noexcept;
    void drawFullscreenTriangle() const noexcept;
    void invalidatePointShadows() noexcept;

    struct PointShadowSlot
    {
        int lightIndex = -1;
        glm::vec3 pos{0.0f};
        float radius = 0.0f;
        float fade = 0.0f;
        bool dirty = true;
    };

    [[nodiscard]] MegaScene &scene() noexcept { return m_scene; }
    [[nodiscard]] const MegaScene &scene() const noexcept { return m_scene; }
    [[nodiscard]] GrassField &grass() noexcept { return m_grass; }
    [[nodiscard]] const GrassField &grass() const noexcept { return m_grass; }

    [[nodiscard]] const GpuBuffer &frameUbo() const noexcept { return m_frameUboRing[m_frameUboSlot]; }
    [[nodiscard]] const GpuBuffer &lightsSSBO() const noexcept { return m_lightsSSBO; }
    [[nodiscard]] const GpuBuffer &flamesSSBO() const noexcept { return m_flamesSSBO; }
    [[nodiscard]] std::uint32_t flameCount() const noexcept { return m_flameCount; }
    [[nodiscard]] GLuint emptyVao() const noexcept { return m_emptyVao; }
    [[nodiscard]] const GpuBuffer &clusterCountsSSBO() const noexcept { return m_clusterCountsSSBO; }
    [[nodiscard]] const GpuBuffer &clusterIndicesSSBO() const noexcept { return m_clusterIndicesSSBO; }
    [[nodiscard]] std::uint32_t lightCount() const noexcept { return m_lightCount; }

    static constexpr int kShadowMapSize = 2048;
    static constexpr float kShadowHalfExtent = 26.0f;
    static constexpr int kPointShadowSize = 256;
    static constexpr int kMaxPointShadowCasters = 4;
    static constexpr float kPointShadowNear = 0.14f;

    [[nodiscard]] GLuint pointShadowCubeArrayTexId() const noexcept { return m_pointShadowCubeArray; }
    [[nodiscard]] const gl::Framebuffer &pointShadowFbo() const noexcept { return m_pointShadowFbo; }
    [[nodiscard]] std::array<PointShadowSlot, kMaxPointShadowCasters> &pointShadowSlots() noexcept { return m_pointShadowSlots; }
    [[nodiscard]] const std::array<PointShadowSlot, kMaxPointShadowCasters> &pointShadowSlots() const noexcept { return m_pointShadowSlots; }

    [[nodiscard]] const gl::Texture &gbuffer0() const noexcept { return m_gbuffer0; }
    [[nodiscard]] const gl::Texture &gbuffer1() const noexcept { return m_gbuffer1; }
    [[nodiscard]] const gl::Texture &depthTex() const noexcept { return m_depthTex; }
    [[nodiscard]] const gl::Texture &hdrTex() const noexcept { return m_hdrTex; }
    [[nodiscard]] const gl::Texture &ssprHashTex() const noexcept { return m_ssprHashTex; }
    [[nodiscard]] const gl::Texture &ssprReflectTex() const noexcept { return m_ssprReflectTex; }
    [[nodiscard]] const gl::Texture &aoRawTex() const noexcept { return m_aoRawTex; }
    [[nodiscard]] const gl::Texture &aoTex() const noexcept { return m_aoTex; }
    [[nodiscard]] const gl::Texture &giScreenTex() const noexcept { return m_giScreenTex; }
    [[nodiscard]] const gl::Texture &ldrTex() const noexcept { return m_ldrTex; }
    [[nodiscard]] const gl::Texture &shadowDepthTex() const noexcept { return m_shadowDepthTex; }

    [[nodiscard]] const gl::Framebuffer &gbufferFbo() const noexcept { return m_gbufferFbo; }
    [[nodiscard]] const gl::Framebuffer &puddleFbo() const noexcept { return m_puddleFbo; }
    [[nodiscard]] const gl::Framebuffer &aoRawFbo() const noexcept { return m_aoRawFbo; }
    [[nodiscard]] const gl::Framebuffer &aoFbo() const noexcept { return m_aoFbo; }
    [[nodiscard]] const gl::Framebuffer &giScreenFbo() const noexcept { return m_giScreenFbo; }
    [[nodiscard]] const gl::Framebuffer &hdrFbo() const noexcept { return m_hdrFbo; }
    [[nodiscard]] const gl::Framebuffer &hdrDepthFbo() const noexcept { return m_hdrDepthFbo; }
    [[nodiscard]] const gl::Framebuffer &ldrFbo() const noexcept { return m_ldrFbo; }
    [[nodiscard]] const gl::Framebuffer &shadowFbo() const noexcept { return m_shadowFbo; }
    [[nodiscard]] GLuint bloomTexId() const noexcept { return m_bloomTex; }
    [[nodiscard]] const gl::Framebuffer &bloomFbo() const noexcept { return m_bloomFbo; }
    [[nodiscard]] glm::ivec2 bloomSize() const noexcept { return m_bloomSize; }
    [[nodiscard]] int bloomMipCount() const noexcept { return m_bloomMipCount; }
    [[nodiscard]] int aoWidth() const noexcept { return m_aoWidth; }
    [[nodiscard]] int aoHeight() const noexcept { return m_aoHeight; }

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

    [[nodiscard]] GLuint fogVolumeTexId(int idx) const noexcept { return m_fogVolumeTex[static_cast<std::size_t>(idx)]; }
    [[nodiscard]] GLuint fogAccumTexId() const noexcept { return m_fogAccumTex; }
    [[nodiscard]] int fogWriteIndex() const noexcept { return m_fogWriteIdx; }
    void advanceFogHistory() noexcept { m_fogWriteIdx = 1 - m_fogWriteIdx; }

    [[nodiscard]] GLuint sampledShadowDepthTexId(bool enabled) const noexcept { return enabled ? m_shadowDepthTex.id() : m_dummyShadowTex; }
    [[nodiscard]] GLuint sampledPointShadowCubeArrayTexId(bool enabled) const noexcept { return enabled ? m_pointShadowCubeArray : m_dummyPointShadowTex; }
    [[nodiscard]] GLuint sampledAoTexId(bool enabled) const noexcept { return enabled ? m_aoTex.id() : m_dummyWhiteR8Tex; }
    [[nodiscard]] GLuint sampledGiScreenTexId(bool enabled) const noexcept { return enabled ? m_giScreenTex.id() : m_dummyBlackTex; }
    [[nodiscard]] GLuint sampledGiShTexId(int idx, bool enabled) const noexcept { return enabled ? m_giShTex[static_cast<std::size_t>(idx)] : m_dummyZero3DTex; }
    [[nodiscard]] GLuint sampledFogAccumTexId(bool enabled) const noexcept { return enabled ? m_fogAccumTex : m_dummyNeutralFog3DTex; }
    [[nodiscard]] GLuint sampledBloomTexId(bool enabled) const noexcept { return enabled ? m_bloomTex : m_dummyBlackTex; }

    [[nodiscard]] GLuint prevHzbTexId() const noexcept { return m_hzbTex[static_cast<std::size_t>(1 - m_hzbWriteIdx)]; }
    [[nodiscard]] GLuint currHzbTexId() const noexcept { return m_hzbTex[static_cast<std::size_t>(m_hzbWriteIdx)]; }
    [[nodiscard]] glm::ivec2 hzbSize() const noexcept { return m_hzbSize; }
    [[nodiscard]] int hzbMipCount() const noexcept { return m_hzbMipCount; }
    void markHzbValid() noexcept { m_hzbValid = true; }
    void advanceHzbHistory() noexcept { m_hzbWriteIdx = 1 - m_hzbWriteIdx; }

    [[nodiscard]] GLuint albedoArrayTexId() const noexcept { return m_albedoArrayTex; }
    [[nodiscard]] GLuint ormArrayTexId() const noexcept { return m_ormArrayTex; }
    [[nodiscard]] GLuint normalArrayTexId() const noexcept { return m_normalArrayTex; }
    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }

private:
    void createStarterMaterialArrays();
    void updatePointShadowSlots(const glm::vec3 &cameraPos, bool allowMultiSwap);

    static constexpr int kMaterialArraySize = 1024;
    static constexpr int kMaterialArrayLayers = 9;
    static constexpr int kMaterialMipLevels = 11;

    int m_width = 0;
    int m_height = 0;
    int m_aoWidth = 0;
    int m_aoHeight = 0;

    static constexpr std::size_t kFrameUboSlots = 4;
    MegaScene m_scene;
    GrassField m_grass;
    std::array<GpuBuffer, kFrameUboSlots> m_frameUboRing;
    std::array<GLsync, kFrameUboSlots> m_frameUboFences{};
    std::size_t m_frameUboSlot = 0;

    GpuBuffer m_lightsSSBO;
    GpuBuffer m_flamesSSBO;
    std::uint32_t m_flameCount = 0;
    GpuBuffer m_clusterCountsSSBO;
    GpuBuffer m_clusterIndicesSSBO;
    std::uint32_t m_lightCount = 0;
    std::vector<GpuPointLight> m_cpuLights;
    std::size_t m_shadowCasterLightCount = 0;
    GLuint m_pointShadowCubeArray = 0;
    gl::Framebuffer m_pointShadowFbo;
    std::array<PointShadowSlot, kMaxPointShadowCasters> m_pointShadowSlots{};
    std::uint64_t m_lastPointShadowSceneGen = 0;
    bool m_pointShadowBurst = true;

    gl::Texture m_gbuffer0; // RGBA8: Albedo.rgb + AO
    gl::Texture m_gbuffer1; // RGBA16F: OctNormal.xy + MatId + SignedRoughness
    gl::Texture m_depthTex; // DEPTH_COMPONENT32F (Reversed-Z [0, 1])
    gl::Texture m_hdrTex;   // RGBA16F: Lit HDR accumulation
    gl::Texture m_ssprHashTex;    // R32UI: Packed planar reflection source pixel + depth priority
    gl::Texture m_ssprReflectTex; // RGBA16F: Hole-filled SSPR + night-sky reflection buffer
    gl::Texture m_aoRawTex;       // R8 half-res raw Hemisphere SSAO
    gl::Texture m_aoTex;          // R8 half-res bilateral-filtered contact SSAO
    gl::Texture m_giScreenTex;    // RGBA16F half-res screen-space L1 SH indirect irradiance + viewZ
    gl::Texture m_ldrTex;         // RGBA8: Tonemapped sRGB + luma in alpha for FXAA
    gl::Texture m_shadowDepthTex; // DEPTH_COMPONENT32F ([0, 1] linear directional shadow map)

    gl::Framebuffer m_gbufferFbo;
    gl::Framebuffer m_puddleFbo; // Color attachments only (no depth attached so depthTex can be sampled safely)
    gl::Framebuffer m_aoRawFbo;
    gl::Framebuffer m_aoFbo;
    gl::Framebuffer m_giScreenFbo;
    gl::Framebuffer m_hdrFbo;
    gl::Framebuffer m_hdrDepthFbo; // HDR color + read-only depthTex for depth-tested forward flame billboards
    gl::Framebuffer m_ldrFbo;
    gl::Framebuffer m_shadowFbo;
    GLuint m_bloomTex = 0;
    gl::Framebuffer m_bloomFbo;
    glm::ivec2 m_bloomSize{0};
    int m_bloomMipCount = 0;

    GLuint m_dummyShadowTex = 0;
    GLuint m_dummyPointShadowTex = 0;
    GLuint m_dummyWhiteR8Tex = 0;
    GLuint m_dummyBlackTex = 0;
    GLuint m_dummyZero3DTex = 0;
    GLuint m_dummyNeutralFog3DTex = 0;

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

    std::array<GLuint, 2> m_fogVolumeTex{0, 0};
    GLuint m_fogAccumTex = 0;
    int m_fogWriteIdx = 0;
    std::array<GLuint, 2> m_hzbTex{0, 0};
    glm::ivec2 m_hzbSize{0};
    int m_hzbMipCount = 0;
    int m_hzbWriteIdx = 0;
    bool m_hzbValid = false;
    std::uint64_t m_lastHzbSceneGen = 0;
    glm::vec3 m_prevCameraPos{0.0f};
    bool m_hasPrevCameraPos = false;
    glm::mat4 m_prevViewProj{1.0f};
    bool m_hasPrevViewProj = false;
};

} // namespace game::render
