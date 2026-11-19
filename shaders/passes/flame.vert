#version 460 core

#include "common/frame_ubo.glsl"

struct FlameInstance
{
    vec4 posWidth;   // xyz = base world pos, w = billboard width
    vec4 heightSeed; // x = height, y = phase seed, z = hdr intensity, w = kind (0=candle, 1=torch)
};

layout(std430, binding = 0) readonly buffer FlamesBuffer
{
    FlameInstance Flames[];
};

out vec2 vUV;
flat out vec4 vParams;

const vec2 kCorners[6] = vec2[6](
    vec2(-1.0, 0.0),
    vec2( 1.0, 0.0),
    vec2( 1.0, 1.0),
    vec2(-1.0, 0.0),
    vec2( 1.0, 1.0),
    vec2(-1.0, 1.0)
);

void main()
{
    FlameInstance inst = Flames[uint(gl_InstanceID)];
    vec2 corner = kCorners[gl_VertexID];

    vec3 basePos = inst.posWidth.xyz;
    float width = inst.posWidth.w;
    float height = inst.heightSeed.x;

    vec3 toCam = uCameraPos.xyz - basePos;
    float dist = length(toCam);
    vec3 forward = (dist > 1e-4) ? (toCam / dist) : vec3(0.0, 0.0, 1.0);
    vec3 horiz = vec3(forward.x, 0.0, forward.z);
    float hLen = length(horiz);
    vec3 right = (hLen > 1e-4) ? normalize(cross(vec3(0.0, 1.0, 0.0), horiz / hLen)) : vec3(1.0, 0.0, 0.0);
    vec3 camUp = cross(forward, right);
    vec3 up = normalize(mix(vec3(0.0, 1.0, 0.0), camUp, 0.30));

    vec3 worldPos = basePos + right * (corner.x * width * 0.5) + up * (corner.y * height);
    vUV = corner;
    vParams = inst.heightSeed;
    gl_Position = uViewProj * vec4(worldPos, 1.0);
}
