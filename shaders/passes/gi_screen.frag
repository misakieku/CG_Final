#version 460 core
in vec2 vUV;

#include "common/frame_ubo.glsl"
#include "common/tonemap.glsl"
#include "common/gbuffer_common.glsl"
#include "common/lighting_common.glsl"
#include "common/gi_common.glsl"

layout(binding = 0) uniform sampler2D uDepthTex;
layout(binding = 1) uniform sampler2D uGBuffer1;
layout(binding = 2) uniform sampler3D uGiSh0;
layout(binding = 3) uniform sampler3D uGiSh1;
layout(binding = 4) uniform sampler3D uGiSh2;
layout(binding = 5) uniform usampler2D uMazeGrid;

out vec4 FragGI;

void main()
{
    ivec2 fullMax = max(ivec2(uScreenParams.xy) - ivec2(1), ivec2(0));
    ivec2 fc = clamp(ivec2(gl_FragCoord.xy) * 2, ivec2(0), fullMax);
    float depth = texelFetch(uDepthTex, fc, 0).r;
    if (depth <= 0.0)
    {
        FragGI = vec4(0.0);
        return;
    }

    vec2 fullUV = (vec2(fc) + 0.5) * uScreenParams.zw;
    vec3 worldPos = reconstructWorldPos(fullUV, depth);
    float viewZ = max(0.01, -(uView * vec4(worldPos, 1.0)).z);
    vec3 N = octDecodeNormal(texelFetch(uGBuffer1, fc, 0).rg);
    vec3 gi = sampleProbeGiFiltered(worldPos, N, uGiSh0, uGiSh1, uGiSh2, uMazeGrid);
    FragGI = vec4(gi, viewZ);
}
