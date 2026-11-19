#version 460 core
in vec3 vSphereN;
in vec3 vWorldPos;
flat in ivec3 vProbeCoord;
flat in uint vProbeState;

#include "common/frame_ubo.glsl"
#include "common/lighting_common.glsl"
#include "common/tonemap.glsl"
#include "common/gi_common.glsl"

layout(binding = 0) uniform sampler3D uGiSh0;
layout(binding = 1) uniform sampler3D uGiSh1;
layout(binding = 2) uniform sampler3D uGiSh2;

out vec4 FragColor;

void main()
{
    vec3 N = normalize(vSphereN);

    if (vProbeState == 0u)
    {
        // Dormant valid probe not yet visited by the camera
        float dome = 0.35 + 0.65 * clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
        FragColor = vec4(vec3(0.012, 0.015, 0.022) * dome, 1.0);
        return;
    }

    vec4 sh0 = texelFetch(uGiSh0, vProbeCoord, 0);
    vec4 sh1 = texelFetch(uGiSh1, vProbeCoord, 0);
    vec4 sh2 = texelFetch(uGiSh2, vProbeCoord, 0);
    vec3 shColor = evalL1Rgb(sh0, sh1, sh2, N);

    FragColor = vec4(max(shColor, vec3(0.002)), 1.0);
}
