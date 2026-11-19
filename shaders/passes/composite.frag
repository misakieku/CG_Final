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
layout(binding = 4) uniform sampler2D uGiScreenTex;
layout(binding = 7) uniform usampler2D uMazeGrid;
layout(binding = 8) uniform sampler2D uSsaoTex;
layout(binding = 9) uniform samplerCubeArrayShadow uPointShadowMaps;

out vec4 FragColor;

float samplePointShadow(uint lightIdx, vec3 worldPos, vec3 lightPos, vec3 N, float NdotL)
{
    if (uShadowParams.w < 0.5)
    {
        return 1.0;
    }
    int slot = -1;
    for (int s = 0; s < 4; ++s)
    {
        if (uPointShadowMeta[s].x == lightIdx)
        {
            slot = s;
            break;
        }
    }
    if (slot < 0)
    {
        return 1.0;
    }

    float nearZ = uintBitsToFloat(uPointShadowMeta[slot].y);
    float farZ = uintBitsToFloat(uPointShadowMeta[slot].z);
    float fade = uintBitsToFloat(uPointShadowMeta[slot].w);
    if (fade <= 1e-4)
    {
        return 1.0;
    }

    vec3 biasedPos = worldPos + N * (0.045 * (1.0 - NdotL) + 0.022);
    vec3 lightToFrag = biasedPos - lightPos;
    float faceZ = max(abs(lightToFrag.x), max(abs(lightToFrag.y), abs(lightToFrag.z)));
    if (faceZ <= nearZ || faceZ >= farZ)
    {
        return 1.0;
    }

    float refZ = (farZ * (faceZ - nearZ)) / (faceZ * (farZ - nearZ)) - 0.0006;
    refZ = clamp(refZ, 0.0, 1.0);

    vec3 dir = lightToFrag / max(length(lightToFrag), 1e-4);
    vec3 upRef = (abs(dir.y) < 0.99) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(upRef, dir));
    vec3 bitangent = cross(dir, tangent);

    float angle = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    float cs = cos(angle);
    float sn = sin(angle);
    float diskRadius = faceZ * 0.0085;

    const vec2 kOffsets[4] = vec2[4](
        vec2(-0.5, -0.5),
        vec2( 0.5, -0.5),
        vec2(-0.5,  0.5),
        vec2( 0.5,  0.5)
    );

    float sum = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        vec2 o = kOffsets[i];
        vec2 rot = vec2(o.x * cs - o.y * sn, o.x * sn + o.y * cs) * diskRadius;
        vec3 sampleVec = lightToFrag + tangent * rot.x + bitangent * rot.y;
        sum += texture(uPointShadowMaps, vec4(sampleVec, float(slot)), refZ);
    }

    return mix(1.0, sum * 0.25, fade);
}

float sampleDirectionalShadow(vec3 worldPos, vec3 N, float NdotL)
{
    if (NdotL <= 0.0)
    {
        return 0.0;
    }
    if (uShadowParams.z < 0.5)
    {
        return 1.0;
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

vec3 sampleHalfResGi(ivec2 coord, float viewZ)
{
    ivec2 halfMax = max(textureSize(uGiScreenTex, 0) - ivec2(1), ivec2(0));
    vec2 halfCoord = vec2(coord) * 0.5;
    ivec2 i0 = clamp(ivec2(floor(halfCoord)), ivec2(0), halfMax);
    vec2 f = clamp(halfCoord - vec2(i0), vec2(0.0), vec2(1.0));

    vec4 s00 = texelFetch(uGiScreenTex, i0, 0);
    vec4 s10 = texelFetch(uGiScreenTex, min(i0 + ivec2(1, 0), halfMax), 0);
    vec4 s01 = texelFetch(uGiScreenTex, min(i0 + ivec2(0, 1), halfMax), 0);
    vec4 s11 = texelFetch(uGiScreenTex, min(i0 + ivec2(1, 1), halfMax), 0);

    vec4 bilinW = vec4(
        (1.0 - f.x) * (1.0 - f.y),
        f.x * (1.0 - f.y),
        (1.0 - f.x) * f.y,
        f.x * f.y
    );

    float invRefZ = 1.0 / max(viewZ, 0.35);
    vec4 tapZ = vec4(s00.a, s10.a, s01.a, s11.a);
    vec4 valid = step(0.001, tapZ);
    vec4 dz = abs(tapZ - viewZ) * invRefZ;
    vec4 depthW = 1.0 / (1.0 + 36.0 * dz * dz);
    vec4 w = (bilinW * depthW + vec4(1e-4)) * valid;

    float wSum = dot(w, vec4(1.0));
    if (wSum <= 1e-6)
    {
        return vec3(0.0);
    }
    return (s00.rgb * w.x + s10.rgb * w.y + s01.rgb * w.z + s11.rgb * w.w) / wSum;
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
    vec3 indirectGi = vec3(0.024, 0.030, 0.046);
    if (uGiDims.w > 0u)
    {
        if ((uFogDims.w & kFeatureGiBit) != 0u)
        {
            float viewZ = max(0.01, -(uView * vec4(worldPos, 1.0)).z);
            indirectGi = sampleHalfResGi(coord, viewZ);
        }
        else
        {
            indirectGi = vec3(0.0);
        }
    }
    float ssao = ((uFogDims.w & kFeatureSsaoBit) != 0u) ? texture(uSsaoTex, vUV).r : 1.0;
    float contactAo = ao * ssao;
    if ((uFogDims.w & kFeatureIndirectOnlyBit) != 0u)
    {
        FragColor = vec4(indirectGi * contactAo, 1.0);
        return;
    }
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
        float atten = computePointLightAttenuation(d2, radius);
        if (atten * light.colorIntensity.w * NdotL < 2e-4)
        {
            continue;
        }
        float wallVis = (uGiDims.w > 0u) ? evalPointLightWallVisibility(worldPos + N * 0.06, light.positionRadius.xyz, uMazeGrid) : 1.0;
        if (wallVis <= 1e-4)
        {
            continue;
        }

        float pointShadow = samplePointShadow(lightIdx, worldPos, light.positionRadius.xyz, N, NdotL);
        if (pointShadow <= 1e-4)
        {
            continue;
        }

        float flicker = computeFlameFlicker(t, lightIdx);
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), shininess) * specScale;
        vec3 radiance = light.colorIntensity.rgb * (light.colorIntensity.w * atten * flicker * wallVis * pointShadow);
        lighting += (diffuseAlbedo * NdotL + specTint * spec) * radiance;
    }

    FragColor = vec4(lighting, 1.0);
}
