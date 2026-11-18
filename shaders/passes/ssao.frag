#version 460 core
in vec2 vUV;

#include "common/frame_ubo.glsl"
#include "common/gbuffer_common.glsl"

layout(binding = 0) uniform sampler2D uDepthTex;
layout(binding = 1) uniform sampler2D uGBuffer1;

out float FragAO;

const int kSampleCount = 12;
const float kRadius = 0.55;
const float kStrength = 1.35;

// Stratified hemisphere offsets (z >= 0.15 so samples never dip below tangent plane)
const vec3 kKernel[12] = vec3[12](
    vec3( 0.04,  0.03,  0.16),
    vec3(-0.08,  0.05,  0.19),
    vec3( 0.10, -0.11,  0.22),
    vec3(-0.15, -0.12,  0.25),
    vec3( 0.24,  0.14,  0.27),
    vec3(-0.28,  0.22,  0.30),
    vec3( 0.16, -0.36,  0.34),
    vec3(-0.38, -0.26,  0.38),
    vec3( 0.52,  0.28,  0.42),
    vec3(-0.48,  0.44,  0.46),
    vec3( 0.34, -0.62,  0.50),
    vec3(-0.62, -0.45,  0.54)
);

void main()
{
    float depth = texture(uDepthTex, vUV).r;
    if (depth <= 0.0)
    {
        FragAO = 1.0;
        return;
    }

    vec3 worldPos = reconstructWorldPos(vUV, depth);
    vec3 N = octDecodeNormal(texture(uGBuffer1, vUV).rg);
    float originViewZ = -(uView * vec4(worldPos, 1.0)).z;

    // Interleaved high-frequency per-pixel rotation around N
    float phi = fract(sin(dot(floor(gl_FragCoord.xy), vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    vec3 upHint = (abs(N.y) < 0.99) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 t0 = normalize(cross(upHint, N));
    vec3 b0 = cross(N, t0);
    float cs = cos(phi);
    float sn = sin(phi);
    vec3 T = t0 * cs + b0 * sn;
    vec3 B = -t0 * sn + b0 * cs;

    float bias = 0.024 + originViewZ * 0.0025;
    float occlusion = 0.0;

    for (int i = 0; i < kSampleCount; ++i)
    {
        vec3 sLocal = kKernel[i];
        vec3 sampleWorld = worldPos + (T * sLocal.x + B * sLocal.y + N * sLocal.z) * kRadius;
        vec4 clip = uViewProj * vec4(sampleWorld, 1.0);
        if (clip.w <= 0.01)
        {
            continue;
        }

        vec2 sampleUV = (clip.xy / clip.w) * 0.5 + 0.5;
        if (sampleUV.x <= 0.0 || sampleUV.x >= 1.0 || sampleUV.y <= 0.0 || sampleUV.y >= 1.0)
        {
            continue;
        }

        float sceneDepth = texture(uDepthTex, sampleUV).r;
        if (sceneDepth <= 0.0)
        {
            continue;
        }

        vec3 sceneWorld = reconstructWorldPos(sampleUV, sceneDepth);
        float sceneViewZ = -(uView * vec4(sceneWorld, 1.0)).z;
        float sampleViewZ = -(uView * vec4(sampleWorld, 1.0)).z;

        float rangeCheck = smoothstep(0.0, 1.0, kRadius / max(abs(originViewZ - sceneViewZ), 1e-4));
        float isOccluded = (sampleViewZ - sceneViewZ > bias) ? 1.0 : 0.0;
        occlusion += isOccluded * rangeCheck;
    }

    float rawAo = 1.0 - (occlusion / float(kSampleCount)) * kStrength;
    FragAO = pow(clamp(rawAo, 0.0, 1.0), 1.30);
}
