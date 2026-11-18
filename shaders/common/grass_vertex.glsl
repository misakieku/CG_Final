#pragma once

#include "common/grass_common.glsl"

layout(std430, binding = 0) readonly buffer GrassInstancesBuffer
{
    GrassInstance GrassInstances[];
};

layout(std430, binding = 2) readonly buffer VisibleGrassIndicesBuffer
{
    uint VisibleGrassIndices[];
};

vec4 computeGrassWorldPos(GrassInstance inst, vec3 localPos, vec3 localNormal, vec2 bladeUV, out vec3 outWorldNormal)
{
    float dist = 0.0;
    float radius = 0.0;
    float screenPct = evalGrassScreenPct(inst, dist, radius);
    float distFade = (dist <= radius) ? 1.0 : smoothstep(0.018, 0.032, screenPct);

    float c = cos(inst.posYaw.w);
    float s = sin(inst.posYaw.w);

    float sx = inst.scaleSeed.x;
    float sy = inst.scaleSeed.y * distFade;
    float leanExtra = inst.scaleSeed.w;

    float t = clamp(bladeUV.y, 0.0, 1.0);
    float scaledX = localPos.x * sx;
    float scaledZ = localPos.z * sx * leanExtra;
    float scaledY = (localPos.y > 0.0) ? (localPos.y * sy) : localPos.y;

    vec3 worldOffset = vec3(
        scaledX * c - scaledZ * s,
        scaledY,
        scaledX * s + scaledZ * c
    );

    vec3 worldPos = inst.posYaw.xyz + worldOffset;

    // Gentle wind gust driven by blade height squared so roots stay anchored in soil
    float bendWeight = t * t * distFade;
    float windTime = uTimeParams.x * 2.1;
    float phaseA = inst.posYaw.x * 0.85 + inst.posYaw.z * 0.65;
    float phaseB = inst.posYaw.x * 1.9 - inst.posYaw.z * 1.4;
    float gust = sin(windTime + phaseA) * 0.055 + sin(windTime * 1.7 + phaseB) * 0.025;
    worldPos.x += gust * bendWeight;
    worldPos.z += (cos(windTime * 0.92 + phaseA) * 0.042 + 0.015) * bendWeight;
    worldPos.y -= abs(gust) * 0.14 * bendWeight;

    vec3 rotN = vec3(
        localNormal.x * c - localNormal.z * s,
        localNormal.y,
        localNormal.x * s + localNormal.z * c
    );
    outWorldNormal = normalize(rotN);
    return vec4(worldPos, 1.0);
}
