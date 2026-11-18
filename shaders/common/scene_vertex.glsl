#pragma once

layout(std430, binding = 0) readonly buffer ObjectsBuffer
{
    DrawObjectData Objects[];
};

layout(std430, binding = 3) readonly buffer VisibleIndicesBuffer
{
    uint VisibleIndices[];
};

vec4 computeSceneWorldPos(DrawObjectData obj, vec3 localPos, vec2 uv)
{
    vec4 worldPos = obj.model * vec4(localPos, 1.0);
    if ((obj.flags & kObjectWindSwayFlag) != 0u)
    {
        float swayWeight = clamp(uv.y, 0.0, 1.0);
        float t = uTimeParams.x * 1.65;
        float phase = worldPos.x * 1.3 + worldPos.z * 1.7 + worldPos.y * 0.9;
        worldPos.x += sin(t + phase) * 0.045 * swayWeight;
        worldPos.z += cos(t * 0.85 + phase * 1.2) * 0.045 * swayWeight;
    }
    return worldPos;
}
