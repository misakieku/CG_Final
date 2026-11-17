#version 460 core
in vec3 vWorldPos;
in vec2 vUV;

#include "common/frame_ubo.glsl"
#include "common/clustered_lights_ro.glsl"

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
    if (sceneDepth > 0.0 && gl_FragCoord.z <= sceneDepth)
    {
        discard;
    }

    vec2 screenUV = gl_FragCoord.xy * uScreenParams.zw;
    float verticalDepth = 0.14;
    float rayDepth = 0.22;
    if (sceneDepth > 0.0)
    {
        vec3 sceneWorldPos = reconstructWorldPos(screenUV, sceneDepth);
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

    // Moonlight + sky reflection
    vec3 moonH = normalize(kMoonDir + V);
    float moonNdotL = max(dot(N, kMoonDir), 0.0);
    float moonSpec = pow(max(dot(N, moonH), 0.0), 190.0) * 1.15;

    vec3 R = reflect(-V, N);
    vec3 skyReflect = mix(vec3(0.020, 0.038, 0.075), vec3(0.085, 0.135, 0.250), clamp(R.y * 0.85 + 0.15, 0.0, 1.0));

    vec3 scatterLight = waterAlbedo * (vec3(0.045, 0.060, 0.095) + kMoonColor * moonNdotL * 0.65);
    vec3 specularLight = kMoonColor * moonSpec;

    // Clustered point lights
    uint clusterId = computeClusterId(screenUV, vWorldPos);
    uint maxPerCluster = uClusterDims.w;
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

        float atten = computePointLightAttenuation(d2, radius);
        float flicker = computeFlameFlicker(t, lightIdx);
        vec3 radiance = light.colorIntensity.rgb * (light.colorIntensity.w * atten * flicker);

        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), 140.0) * 0.85;
        scatterLight += waterAlbedo * (NdotL * radiance);
        specularLight += radiance * spec;
    }

    float shoreMeniscus = smoothstep(0.018, 0.002, verticalDepth) * smoothstep(0.0, 0.004, verticalDepth) * 0.14;
    vec3 color = mix(scatterLight, skyReflect, fresnel * 0.78) + specularLight + vec3(0.28, 0.42, 0.44) * shoreMeniscus;

    float alpha = clamp(max(absorb * 0.86, fresnel * 0.72), 0.08, 0.95) * edgeFeather;
    FragColor = vec4(color, alpha);
}
