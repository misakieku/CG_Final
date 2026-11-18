#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

#include "common/frame_ubo.glsl"
#include "common/scene_types.glsl"
#include "common/scene_vertex.glsl"

invariant gl_Position;

out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec2 vUV;
flat out uint vMaterialId;
flat out uint vFlags;
flat out uint vObjectId;

void main()
{
    uint objId = VisibleIndices[uint(gl_BaseInstance) + uint(gl_InstanceID)];
    DrawObjectData obj = Objects[objId];
    vec4 worldPos = computeSceneWorldPos(obj, aPos, aUV);

    vWorldPos = worldPos.xyz;
    vWorldNormal = normalize(mat3(obj.normalMatrix) * aNormal);
    vUV = aUV;
    vMaterialId = obj.materialId;
    vFlags = obj.flags;
    vObjectId = objId;

    gl_Position = uViewProj * worldPos;
}
