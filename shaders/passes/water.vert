#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

#include "common/frame_ubo.glsl"
#include "common/scene_types.glsl"

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
