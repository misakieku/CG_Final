#pragma once

struct GpuPointLight
{
    vec4 positionRadius; // xyz = world position, w = radius
    vec4 colorIntensity; // rgb = linear color, w = intensity
};

#define kMoonDir (uMoonDir.xyz)
const vec3 kMoonColor = vec3(0.32, 0.44, 0.78);

uint computeClusterId(vec2 screenUV, vec3 worldPos)
{
    uint nx = uClusterDims.x;
    uint ny = uClusterDims.y;
    uint nz = uClusterDims.z;

    float nearZ = max(uTimeParams.z, 1e-4);
    float farZ = max(uTimeParams.w, nearZ + 1.0);
    vec3 viewPos = (uView * vec4(worldPos, 1.0)).xyz;
    float linearZ = clamp(-viewPos.z, nearZ, farZ * 0.9999);

    uint cx = min(uint(screenUV.x * float(nx)), nx - 1u);
    uint cy = min(uint(screenUV.y * float(ny)), ny - 1u);
    float sliceNorm = log(linearZ / nearZ) / log(farZ / nearZ);
    uint cz = min(uint(max(sliceNorm, 0.0) * float(nz)), nz - 1u);
    return cz * (nx * ny) + cy * nx + cx;
}

float computePointLightAttenuation(float distSq, float radius)
{
    float ratio2 = distSq / (radius * radius);
    float window = clamp(1.0 - ratio2 * ratio2, 0.0, 1.0);
    return (window * window) / (distSq + 0.40);
}

float computeFlameFlicker(float time, uint lightIdx)
{
    return 0.90 + 0.10 * sin(time * 7.2 + float(lightIdx) * 2.37) * cos(time * 11.1 + float(lightIdx) * 1.13);
}
