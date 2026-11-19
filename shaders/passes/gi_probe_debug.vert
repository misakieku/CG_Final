#version 460 core
layout(location = 0) in vec3 aUnitNormal;

#include "common/frame_ubo.glsl"
#include "common/lighting_common.glsl"
#include "common/tonemap.glsl"
#include "common/gi_common.glsl"

layout(std430, binding = 7) readonly buffer GiProbeMetaBuffer
{
    uint ProbeMeta[];
};

layout(std430, binding = 8) readonly buffer GiActiveFlagBuffer
{
    uint ActiveFlags[];
};

uniform float uProbeRadius = 0.11;

out vec3 vSphereN;
out vec3 vWorldPos;
flat out ivec3 vProbeCoord;
flat out uint vProbeState;

void main()
{
    uint probeId = uint(gl_InstanceID);
    if (probeId >= uGiDims.w)
    {
        gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    uint meta = ProbeMeta[probeId];
    if (meta == 0u)
    {
        gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    ivec3 pc = giCoordFromIndex(probeId);
    vec3 probePos = giProbeToWorld(pc);
    vec3 toCam = probePos - uCameraPos.xyz;
    if (dot(toCam, toCam) > 484.0)
    {
        gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 worldPos = probePos + aUnitNormal * uProbeRadius;
    vSphereN = aUnitNormal;
    vWorldPos = worldPos;
    vProbeCoord = pc;
    vProbeState = (meta == 1u) ? 0u : ((ActiveFlags[probeId] != 0u) ? 2u : 1u);
    gl_Position = uViewProj * vec4(worldPos, 1.0);
}
