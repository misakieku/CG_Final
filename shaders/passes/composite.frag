#version 460 core
in vec2 vUV;

#include "common/frame_ubo.glsl"
#include "common/clustered_lights_ro.glsl"
#include "common/tonemap.glsl"
#include "common/gi_common.glsl"
#include "common/gbuffer_common.glsl"

layout(binding = 0) uniform sampler2D uGBuffer0; // Albedo.rgb + AO
layout(binding = 1) uniform sampler2D uGBuffer1; // OctNormal.xy + MatId + SignedRoughness
layout(binding = 2) uniform sampler2D uDepthTex; // Reversed-Z [0, 1]
layout(binding = 3) uniform sampler2DShadow uShadowMap;
layout(binding = 4) uniform sampler3D uGiSh0;
layout(binding = 5) uniform sampler3D uGiSh1;
layout(binding = 6) uniform sampler3D uGiSh2;
layout(binding = 7) uniform usampler2D uMazeGrid;
layout(binding = 8) uniform sampler2D uSsaoTex;

out vec4 FragColor;

float sampleDirectionalShadow(vec3 worldPos, vec3 N, float NdotL)
{
    if (uShadowParams.z < 0.5 || NdotL <= 0.0)
    {
        return 0.0;
    }

    vec3 biasedPos = worldPos + N * (0.045 * (1.0 - NdotL) + 0.015);
    vec4 sc = uLightViewProj * vec4(biasedPos, 1.0);
    sc.xyz /= sc.w;

    vec2 uv = sc.xy * 0.5 + 0.5;
    float refZ = sc.z;
    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0 || refZ <= 0.0 || refZ >= 1.0)
    {
        return 1.0;
    }

    float texelScale = uShadowParams.x * 1.25;
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 offset = vec2(float(x), float(y)) * texelScale;
            sum += texture(uShadowMap, vec3(uv + offset, refZ));
        }
    }

    float edge = max(abs(sc.x), abs(sc.y));
    float fade = smoothstep(0.88, 1.0, edge);
    return mix(sum / 9.0, 1.0, fade);
}

void main()
{
    float depth = texture(uDepthTex, vUV).r;
    if (depth <= 0.0)
    {
        vec3 rayWorld = reconstructWorldPos(vUV, 0.5);
        vec3 skyDir = normalize(rayWorld - uCameraPos.xyz);
        FragColor = vec4(evalNightSkyDetailed(skyDir, uTimeParams.x), 1.0);
        return;
    }

    ivec2 coord = ivec2(gl_FragCoord.xy);
    vec4 g0 = texelFetch(uGBuffer0, coord, 0);
    vec4 g1 = texelFetch(uGBuffer1, coord, 0);

    vec3 albedo = g0.rgb;
    float ao = g0.a;
    vec3 N = octDecodeNormal(g1.rg);
    bool isMetal = (g1.a < 0.0);
    float roughness = clamp(abs(g1.a), 0.04, 1.0);

    vec3 diffuseAlbedo = isMetal ? (albedo * 0.18) : albedo;
    vec3 specTint = isMetal ? albedo : vec3(1.0);

    vec3 worldPos = reconstructWorldPos(vUV, depth);
    vec3 V = normalize(uCameraPos.xyz - worldPos);
    float shininess = mix(128.0, 10.0, roughness * roughness);
    float specScale = isMetal ? mix(1.45, 0.45, roughness) : ((1.0 - roughness) * 0.55);

    // Shadowed cool night moonlight + sparse multi-bounce 3D L1 SH Probe GI
    vec3 moonH = normalize(kMoonDir + V);
    float moonNdotL = max(dot(N, kMoonDir), 0.0);
    float moonSpec = pow(max(dot(N, moonH), 0.0), shininess) * specScale;
    float moonShadow = sampleDirectionalShadow(worldPos, N, moonNdotL);
    vec3 indirectGi = (uGiDims.w > 0u)
        ? sampleProbeGiFiltered(worldPos, N, uGiSh0, uGiSh1, uGiSh2, uMazeGrid)
        : vec3(0.024, 0.030, 0.046);
    float ssao = texture(uSsaoTex, vUV).r;
    float contactAo = ao * ssao;
    float directContact = mix(1.0, ssao, 0.25);
    vec3 giTint = isMetal ? (albedo * 0.85) : diffuseAlbedo;
    vec3 lighting = giTint * (indirectGi + vec3(0.006, 0.008, 0.012)) * contactAo
                  + (diffuseAlbedo * moonNdotL + specTint * moonSpec) * kMoonColor * (0.78 * moonShadow * directContact);

    // Clustered point lights (with C1 bilinear grid wall occlusion so torches do not bleed through solid walls)
    uint clusterId = computeClusterId(vUV, worldPos);
    uint maxPerCluster = uClusterDims.w;
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
        float wallVis = (uGiDims.w > 0u) ? evalPointLightWallVisibility(worldPos + N * 0.06, light.positionRadius.xyz, uMazeGrid) : 1.0;
        if (wallVis <= 1e-4)
        {
            continue;
        }

        float atten = computePointLightAttenuation(d2, radius);
        float flicker = computeFlameFlicker(t, lightIdx);
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), shininess) * specScale;
        vec3 radiance = light.colorIntensity.rgb * (light.colorIntensity.w * atten * flicker * wallVis);
        lighting += (diffuseAlbedo * NdotL + specTint * spec) * radiance;
    }

    FragColor = vec4(lighting, 1.0);
}
