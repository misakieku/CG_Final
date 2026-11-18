#version 460 core
layout(early_fragment_tests) in;

in vec3 vWorldNormal;
in vec2 vBladeUV;
flat in float vColorSeed;

#include "common/frame_ubo.glsl"
#include "common/gbuffer_common.glsl"

layout(location = 0) out vec4 outGBuffer0; // Albedo.rgb + AO
layout(location = 1) out vec4 outGBuffer1; // OctNormal.xy + MatId + SignedRoughness

void main()
{
    vec3 geomN = gl_FrontFacing ? normalize(vWorldNormal) : -normalize(vWorldNormal);
    vec3 softNormal = normalize(mix(geomN, vec3(0.0, 1.0, 0.0), 0.62));

    if (uGiMisc.w > 0.0)
    {
        outGBuffer0 = vec4(vec3(uGiMisc.w), 1.0);
        outGBuffer1 = vec4(octEncodeNormal(softNormal), 7.0, 0.78);
        return;
    }

    float t = clamp(vBladeUV.y, 0.0, 1.0);
    float centerRib = 1.0 - abs(vBladeUV.x);

    vec3 rootCol = vec3(0.105, 0.185, 0.068);
    vec3 midCold = vec3(0.145, 0.300, 0.098);
    vec3 midWarm = vec3(0.215, 0.355, 0.112);
    vec3 tipCold = vec3(0.245, 0.445, 0.148);
    vec3 tipWarm = vec3(0.355, 0.485, 0.165);

    vec3 midCol = mix(midCold, midWarm, vColorSeed);
    vec3 tipCol = mix(tipCold, tipWarm, vColorSeed);

    vec3 albedo = (t < 0.45)
        ? mix(rootCol, midCol, t / 0.45)
        : mix(midCol, tipCol, (t - 0.45) / 0.55);
    albedo *= 0.92 + 0.14 * centerRib * t;

    float ao = mix(0.60, 0.96, t);
    float roughness = mix(0.72, 0.54, t);

    outGBuffer0 = vec4(albedo, ao);
    outGBuffer1 = vec4(octEncodeNormal(softNormal), 7.0, roughness);
}
