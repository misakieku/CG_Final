#include "game/render/passes/CompositePass.hpp"

#include <glad/gl.h>

namespace game::render
{

namespace
{

constexpr const char *kFullscreenVertSrc = R"glsl(
#version 460 core
out vec2 vUV;

void main()
{
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

constexpr const char *kCompositeFragSrc = R"glsl(
#version 460 core
in vec2 vUV;

layout(std140, binding = 0) uniform FrameUBO
{
    mat4 uView;
    mat4 uProj;
    mat4 uInvProj;
    mat4 uViewProj;
    mat4 uInvViewProj;
    vec4 uCameraPos;
    vec4 uFrustumPlanes[6];
    vec4 uScreenParams;
    vec4 uTimeParams;
    uvec4 uCounts;
    uvec4 uClusterDims;
};

struct GpuPointLight
{
    vec4 positionRadius;
    vec4 colorIntensity;
};

layout(std430, binding = 4) readonly buffer PointLightsBuffer
{
    GpuPointLight Lights[];
};

layout(std430, binding = 5) readonly buffer ClusterCountsBuffer
{
    uint ClusterCounts[];
};

layout(std430, binding = 6) readonly buffer ClusterIndicesBuffer
{
    uint ClusterLightIndices[];
};

layout(binding = 0) uniform sampler2D uGBuffer0; // Albedo.rgb + AO
layout(binding = 1) uniform sampler2D uGBuffer1; // Normal.xyz + Roughness
layout(binding = 2) uniform sampler2D uDepthTex; // Reversed-Z [0, 1]

out vec4 FragColor;

void main()
{
    float depth = texture(uDepthTex, vUV).r;
    vec3 skyColor = vec3(0.012, 0.018, 0.036);
    if (depth <= 0.0)
    {
        FragColor = vec4(skyColor, 1.0);
        return;
    }

    vec4 g0 = texture(uGBuffer0, vUV);
    vec4 g1 = texture(uGBuffer1, vUV);

    vec3 albedo = g0.rgb;
    float ao = g0.a;
    vec3 N = normalize(g1.rgb);
    float roughness = clamp(g1.a, 0.04, 1.0);

    vec4 ndc = vec4(vUV * 2.0 - 1.0, depth, 1.0);
    vec4 worldH = uInvViewProj * ndc;
    vec3 worldPos = worldH.xyz / worldH.w;
    vec3 V = normalize(uCameraPos.xyz - worldPos);
    float shininess = mix(128.0, 10.0, roughness * roughness);
    float specScale = (1.0 - roughness) * 0.55;

    // Subtle cool night moonlight + faint night ambient
    vec3 moonDir = normalize(vec3(0.45, 0.85, 0.35));
    vec3 moonH = normalize(moonDir + V);
    float moonNdotL = max(dot(N, moonDir), 0.0);
    float moonSpec = pow(max(dot(N, moonH), 0.0), shininess) * specScale;
    vec3 moonColor = vec3(0.32, 0.44, 0.78);
    vec3 lighting = albedo * vec3(0.04, 0.05, 0.08) * ao + (albedo * moonNdotL + vec3(moonSpec)) * moonColor * 0.65;

    // Clustered point lights (torches, candles, treasure & exit beacons)
    uint nx = uClusterDims.x;
    uint ny = uClusterDims.y;
    uint nz = uClusterDims.z;
    uint maxPerCluster = uClusterDims.w;

    float nearZ = max(uTimeParams.z, 1e-4);
    float farZ  = max(uTimeParams.w, nearZ + 1.0);
    vec3 viewPos = (uView * vec4(worldPos, 1.0)).xyz;
    float linearZ = clamp(-viewPos.z, nearZ, farZ * 0.9999);

    uint cx = min(uint(vUV.x * float(nx)), nx - 1u);
    uint cy = min(uint(vUV.y * float(ny)), ny - 1u);
    float sliceNorm = log(linearZ / nearZ) / log(farZ / nearZ);
    uint cz = min(uint(max(sliceNorm, 0.0) * float(nz)), nz - 1u);
    uint clusterId = cz * (nx * ny) + cy * nx + cx;

    uint count = min(ClusterCounts[clusterId], maxPerCluster);
    uint baseOffset = clusterId * maxPerCluster;
    float t = uTimeParams.x;

    for (uint k = 0u; k < count; ++k)
    {
        uint lightIdx = ClusterLightIndices[baseOffset + k];
        GpuPointLight light = Lights[lightIdx];

        vec3 toLight = light.positionRadius.xyz - worldPos;
        float d2 = dot(toLight, toLight);
        float radius = light.positionRadius.w;
        if (d2 >= radius * radius || d2 <= 1e-6)
        {
            continue;
        }

        float d = sqrt(d2);
        vec3 L = toLight / d;
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0)
        {
            continue;
        }

        float ratio2 = d2 / (radius * radius);
        float window = clamp(1.0 - ratio2 * ratio2, 0.0, 1.0);
        float atten = (window * window) / (d2 + 0.40);

        float flicker = 0.90 + 0.10 * sin(t * 7.2 + float(lightIdx) * 2.37) * cos(t * 11.1 + float(lightIdx) * 1.13);
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), shininess) * specScale;
        vec3 radiance = light.colorIntensity.rgb * (light.colorIntensity.w * atten * flicker);
        lighting += (albedo * NdotL + vec3(spec)) * radiance;
    }

    FragColor = vec4(lighting, 1.0);
}
)glsl";

} // namespace

CompositePass::CompositePass()
    : m_compositeShader(gl::Shader::compile(kFullscreenVertSrc, kCompositeFragSrc))
{
}

void CompositePass::execute(const RenderContext &ctx, RenderResources &res)
{
    res.hdrFbo().bind();
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    m_compositeShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    res.lightsSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 4);
    res.clusterCountsSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 5);
    res.clusterIndicesSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 6);

    glBindTextureUnit(0, res.gbuffer0().id());
    glBindTextureUnit(1, res.gbuffer1().id());
    glBindTextureUnit(2, res.depthTex().id());

    res.drawFullscreenTriangle();
}

} // namespace game::render
