#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUV;

#include "common/frame_ubo.glsl"
#include "common/scene_types.glsl"
#include "common/scene_vertex.glsl"

void main()
{
    uint objId = VisibleIndices[uint(gl_BaseInstance) + uint(gl_InstanceID)];
    DrawObjectData obj = Objects[objId];
    vec4 worldPos = computeSceneWorldPos(obj, aPos, aUV);
    gl_Position = uLightViewProj * worldPos;
}
