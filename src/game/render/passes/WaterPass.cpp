#include "game/render/passes/WaterPass.hpp"

#include <glad/gl.h>

#include <cstdint>

namespace game::render
{

namespace
{

constexpr const char *kWaterVertSrc = R"glsl(
#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

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

struct DrawObjectData
{
    mat4 model;
    mat4 normalMatrix;
    vec4 worldSphere;
    uint meshId;
    uint materialId;
    uint flags;
    uint pad;
};

layout(std430, binding = 0) readonly buffer ObjectsBuffer
{
    DrawObjectData Objects[];
};

out vec3 vWorldPos;
out vec2 vUV;

void main()
{
    DrawObjectData obj = Objects[uint(gl_BaseInstance)];
    vec4 worldPos = obj.model * vec4(aPos, 1.0);

    float t = uTimeParams.x;
    worldPos.y += sin(t * 2.1 + worldPos.x * 3.4) * cos(t * 1.7 + worldPos.z * 2.9) * 0.004;

    vWorldPos = worldPos.xyz;
    vUV = aUV;
    gl_Position = uViewProj * worldPos;
}
)glsl";

constexpr const char *kWaterFragSrc = R"glsl(
#version 460 core
in vec3 vWorldPos;
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

layout(binding = 0) uniform sampler2D uDepthTex; // Reversed-Z [0, 1] opaque scene depth

out vec4 FragColor;

vec3 computeWaveNormal(vec2 p, float t)
{
    float dHx = 0.0;
    float dHz = 0.0;

    float a1 = p.x * 5.4 + p.y * 3.1 + t * 1.45;
    dHx += 5.4 * 0.0055 * cos(a1);
    dHz += 3.1 * 0.0055 * cos(a1);

    float a2 = p.x * -4.2 + p.y * 6.3 - t * 1.15;
    dHx += -4.2 * 0.0042 * cos(a2);
    dHz += 6.3 * 0.0042 * cos(a2);

    float a3 = p.x * 9.8 - p.y * 7.6 + t * 2.05;
    dHx += 9.8 * 0.0022 * cos(a3);
    dHz += -7.6 * 0.0022 * cos(a3);

    float a4 = p.x * -11.5 - p.y * 10.2 - t * 2.40;
    dHx += -11.5 * 0.0016 * cos(a4);
    dHz += -10.2 * 0.0016 * cos(a4);

    return normalize(vec3(-dHx, 1.0, -dHz));
}

void main()
{
    float sceneDepth = texelFetch(uDepthTex, ivec2(gl_FragCoord.xy), 0).r;
    // Manual Reversed-Z occlusion test against opaque G-Buffer depth (prevents FBO depth feedback loop)
    if (sceneDepth > 0.0 && gl_FragCoord.z <= sceneDepth)
    {
        discard;
    }

    vec2 screenUV = gl_FragCoord.xy * uScreenParams.zw;
    float verticalDepth = 0.14;
    float rayDepth = 0.22;
    if (sceneDepth > 0.0)
    {
        vec4 sceneNdc = vec4(screenUV * 2.0 - 1.0, sceneDepth, 1.0);
        vec4 sceneWorldH = uInvViewProj * sceneNdc;
        vec3 sceneWorldPos = sceneWorldH.xyz / sceneWorldH.w;
        verticalDepth = max(0.0, vWorldPos.y - sceneWorldPos.y);
        rayDepth = max(0.0, length(sceneWorldPos - vWorldPos));
    }

    float edgeFeather = smoothstep(0.0, 0.014, verticalDepth);
    if (edgeFeather <= 0.001)
    {
        discard;
    }

    float t = uTimeParams.x;
    vec3 N = computeWaveNormal(vWorldPos.xz, t);
    vec3 V = normalize(uCameraPos.xyz - vWorldPos);
    float NdotV = max(dot(N, V), 0.0);
    float fresnel = 0.03 + 0.97 * pow(1.0 - NdotV, 5.0);

    // Beer-Lambert shallow-to-deep absorption gradient
    float absorb = 1.0 - exp(-verticalDepth * 15.0 - rayDepth * 4.2);
    vec3 shallowTint = vec3(0.09, 0.42, 0.43);
    vec3 deepTint = vec3(0.018, 0.13, 0.18);
    vec3 waterAlbedo = mix(shallowTint, deepTint, clamp(absorb, 0.0, 1.0));

    // Moonlight diffuse + sharp ripple specular glints + night sky reflection
    vec3 moonDir = normalize(vec3(0.45, 0.85, 0.35));
    vec3 moonColor = vec3(0.32, 0.44, 0.78);
    vec3 moonH = normalize(moonDir + V);
    float moonNdotL = max(dot(N, moonDir), 0.0);
    float moonSpec = pow(max(dot(N, moonH), 0.0), 190.0) * 1.15;

    vec3 R = reflect(-V, N);
    vec3 skyReflect = mix(vec3(0.020, 0.038, 0.075), vec3(0.085, 0.135, 0.250), clamp(R.y * 0.85 + 0.15, 0.0, 1.0));

    vec3 scatterLight = waterAlbedo * (vec3(0.045, 0.060, 0.095) + moonColor * moonNdotL * 0.65);
    vec3 specularLight = moonColor * moonSpec;

    // Clustered point lights (shore ember, torches, candles dancing across ripples)
    uint nx = uClusterDims.x;
    uint ny = uClusterDims.y;
    uint nz = uClusterDims.z;
    uint maxPerCluster = uClusterDims.w;

    float nearZ = max(uTimeParams.z, 1e-4);
    float farZ = max(uTimeParams.w, nearZ + 1.0);
    vec3 viewPos = (uView * vec4(vWorldPos, 1.0)).xyz;
    float linearZ = clamp(-viewPos.z, nearZ, farZ * 0.9999);

    uint cx = min(uint(screenUV.x * float(nx)), nx - 1u);
    uint cy = min(uint(screenUV.y * float(ny)), ny - 1u);
    float sliceNorm = log(linearZ / nearZ) / log(farZ / nearZ);
    uint cz = min(uint(max(sliceNorm, 0.0) * float(nz)), nz - 1u);
    uint clusterId = cz * (nx * ny) + cy * nx + cx;

    uint count = min(ClusterCounts[clusterId], maxPerCluster);
    uint baseOffset = clusterId * maxPerCluster;
    for (uint k = 0u; k < count; ++k)
    {
        uint lightIdx = ClusterLightIndices[baseOffset + k];
        GpuPointLight light = Lights[lightIdx];
        vec3 toLight = light.positionRadius.xyz - vWorldPos;
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
        vec3 radiance = light.colorIntensity.rgb * (light.colorIntensity.w * atten * flicker);

        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), 140.0) * 0.85;
        scatterLight += waterAlbedo * (NdotL * radiance);
        specularLight += radiance * spec;
    }

    // Subtle soft wet meniscus shimmer at the shoreline
    float shoreMeniscus = smoothstep(0.018, 0.002, verticalDepth) * smoothstep(0.0, 0.004, verticalDepth) * 0.14;
    vec3 color = mix(scatterLight, skyReflect, fresnel * 0.78) + specularLight + vec3(0.28, 0.42, 0.44) * shoreMeniscus;

    float alpha = clamp(max(absorb * 0.86, fresnel * 0.72), 0.08, 0.95) * edgeFeather;
    FragColor = vec4(color, alpha);
}
)glsl";

} // namespace

WaterPass::WaterPass()
    : m_waterShader(gl::Shader::compile(kWaterVertSrc, kWaterFragSrc))
{
}

void WaterPass::execute(const RenderContext &ctx, RenderResources &res)
{
    const MegaScene &scene = res.scene();
    const std::span<const std::uint32_t> waterIds = scene.waterObjectIds();
    if (waterIds.empty())
    {
        return;
    }

    res.hdrFbo().bind();
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    m_waterShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    scene.bindSceneBuffers(0, 1, 2, 3);
    res.lightsSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 4);
    res.clusterCountsSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 5);
    res.clusterIndicesSSBO().bindBase(GL_SHADER_STORAGE_BUFFER, 6);
    glBindTextureUnit(0, res.depthTex().id());

    scene.bindMegaVao();
    for (const std::uint32_t objId : waterIds)
    {
        const DrawObjectData &obj = scene.object(objId);
        if ((obj.flags & kObjectVisibleFlag) == 0u)
        {
            continue;
        }
        const MeshSpan &mesh = scene.meshSpan(obj.meshId);
        const void *indexOffset = reinterpret_cast<const void *>(static_cast<std::uintptr_t>(mesh.firstIndex) * sizeof(std::uint32_t));
        glDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES, static_cast<GLsizei>(mesh.indexCount), GL_UNSIGNED_INT, indexOffset, 1, mesh.baseVertex, objId);
    }

    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

} // namespace game::render
