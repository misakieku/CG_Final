#version 460 core
in vec2 vUV;

#include "common/frame_ubo.glsl"
#include "common/clustered_lights_ro.glsl"
#include "common/tonemap.glsl"

layout(binding = 0) uniform sampler2D uGBuffer0; // Albedo.rgb + AO
layout(binding = 1) uniform sampler2D uGBuffer1; // Normal.xyz + Roughness
layout(binding = 2) uniform sampler2D uDepthTex; // Reversed-Z [0, 1]

out vec4 FragColor;

void main()
{
    float depth = texture(uDepthTex, vUV).r;
    if (depth <= 0.0)
    {
        FragColor = vec4(kNightSkyColor, 1.0);
        return;
    }

    vec4 g0 = texture(uGBuffer0, vUV);
    vec4 g1 = texture(uGBuffer1, vUV);

    vec3 albedo = g0.rgb;
    float ao = g0.a;
    vec3 N = normalize(g1.rgb);
    float roughness = clamp(g1.a, 0.04, 1.0);

    vec3 worldPos = reconstructWorldPos(vUV, depth);
    vec3 V = normalize(uCameraPos.xyz - worldPos);
    float shininess = mix(128.0, 10.0, roughness * roughness);
    float specScale = (1.0 - roughness) * 0.55;

    // Cool night moonlight + subtle ambient
    vec3 moonH = normalize(kMoonDir + V);
    float moonNdotL = max(dot(N, kMoonDir), 0.0);
    float moonSpec = pow(max(dot(N, moonH), 0.0), shininess) * specScale;
    vec3 lighting = albedo * vec3(0.04, 0.05, 0.08) * ao + (albedo * moonNdotL + vec3(moonSpec)) * kMoonColor * 0.65;

    // Clustered point lights
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
        float flicker = computeFlameFlicker(t, lightIdx);
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), shininess) * specScale;
        vec3 radiance = light.colorIntensity.rgb * (light.colorIntensity.w * atten * flicker);
        lighting += (albedo * NdotL + vec3(spec)) * radiance;
    }

    FragColor = vec4(lighting, 1.0);
}
