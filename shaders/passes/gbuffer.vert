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

layout(std430, binding = 3) readonly buffer VisibleIndicesBuffer
{
    uint VisibleIndices[];
};

out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec2 vUV;
flat out uint vMaterialId;
flat out uint vFlags;

void main()
{
    uint objId = VisibleIndices[uint(gl_BaseInstance) + uint(gl_InstanceID)];
    DrawObjectData obj = Objects[objId];

    vec4 worldPos = obj.model * vec4(aPos, 1.0);
    if ((obj.flags & kObjectWindSwayFlag) != 0u)
    {
        float swayWeight = clamp(aUV.y, 0.0, 1.0);
        float t = uTimeParams.x * 1.65;
        float phase = worldPos.x * 1.3 + worldPos.z * 1.7 + worldPos.y * 0.9;
        worldPos.x += sin(t + phase) * 0.045 * swayWeight;
        worldPos.z += cos(t * 0.85 + phase * 1.2) * 0.045 * swayWeight;
    }

    vWorldPos = worldPos.xyz;
    vWorldNormal = normalize(mat3(obj.normalMatrix) * aNormal);
    vUV = aUV;
    vMaterialId = obj.materialId;
    vFlags = obj.flags;

    gl_Position = uViewProj * worldPos;
}
