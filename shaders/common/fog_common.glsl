#pragma once

float fogSliceToLinearZ(float normSlice, float nearZ, float fogFar)
{
    return nearZ * pow(fogFar / nearZ, normSlice);
}

vec3 fogLinearZToUVW(vec2 uv, float linearZ, float nearZ, float fogFar, uint slicesZ)
{
    float clampedZ = clamp(linearZ, nearZ, fogFar);
    float s = log(clampedZ / nearZ) / log(fogFar / nearZ);
    float fSlices = float(slicesZ);
    float w = clamp((s * fSlices - 0.5) / fSlices, 0.5 / fSlices, 1.0 - 0.5 / fSlices);
    return vec3(clamp(uv, vec2(0.0), vec2(1.0)), w);
}

float evalHgPhase(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4);
    // Scaled by PI to stay energy-consistent with our pre-PI direct lighting radiance
    return (1.0 - g2) / (4.0 * pow(denom, 1.5));
}

vec3 reconstructFogWorldPos(vec2 uv, float linearZ)
{
    vec4 clipDepth = uProj * vec4(0.0, 0.0, -linearZ, 1.0);
    float ndcZ = clipDepth.z / clipDepth.w;
    vec4 worldH = uInvViewProj * vec4(uv * 2.0 - 1.0, ndcZ, 1.0);
    return worldH.xyz / worldH.w;
}
