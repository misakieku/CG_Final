#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

#include "common/frame_ubo.glsl"
#include "common/grass_vertex.glsl"

invariant gl_Position;

out vec3 vWorldNormal;
out vec2 vBladeUV;
flat out float vColorSeed;

void main()
{
    uint idx = VisibleGrassIndices[uint(gl_InstanceID)];
    GrassInstance inst = GrassInstances[idx];
    vec3 worldN;
    vec4 worldPos = computeGrassWorldPos(inst, aPos, aNormal, aUV, worldN);

    vWorldNormal = worldN;
    vBladeUV = aUV;
    vColorSeed = inst.scaleSeed.z;
    gl_Position = uViewProj * worldPos;
}
