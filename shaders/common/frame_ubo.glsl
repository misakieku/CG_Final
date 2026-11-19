#pragma once

const uint kFeatureGiBit = 1u;
const uint kFeatureSsaoBit = 2u;
const uint kFeatureFogBit = 4u;
const uint kFeatureBloomBit = 8u;
const uint kFeatureSsrBit = 16u;
const uint kFeatureShadowsBit = 32u;
const uint kFeaturePointShadowsBit = 64u;
const uint kFeatureIndirectOnlyBit = 128u;

layout(std140, binding = 0) uniform FrameUBO
{
    mat4 uView;
    mat4 uProj;
    mat4 uInvProj;
    mat4 uViewProj;
    mat4 uInvViewProj;
    vec4 uCameraPos;
    vec4 uFrustumPlanes[6];
    vec4 uScreenParams; // x = width, y = height, z = 1/width, w = 1/height
    vec4 uTimeParams;   // x = elapsedTime, y = deltaTime, z = nearPlane, w = farPlane
    uvec4 uCounts;      // x = objectCount, y = meshCount, z = lightCount, w = frameIndex
    uvec4 uClusterDims; // x = NX, y = NY, z = NZ, w = maxLightsPerCluster
    mat4 uLightViewProj;
    vec4 uShadowFrustumPlanes[6];
    vec4 uShadowParams;  // x = invShadowSize, y = halfExtent, z = dirShadowEnabled, w = pointShadowEnabled
    vec4 uMoonDir;       // xyz = normalized direction toward moon
    vec4 uGiGridOrigin;  // xyz = probe(0,0,0) world pos, w = probeStepXZ
    vec4 uGiGridInvStep; // xyz = 1/probeStep, w = cellSize
    uvec4 uGiDims;       // x = nx, y = ny, z = nz, w = totalProbes
    vec4 uGiMisc;        // x = wallHeight, y = gridW, z = gridH, w = whiteboxAlbedo
    mat4 uPrevViewProj;
    vec4 uFogParams;     // x = density, y = anisotropy, z = heightFalloff, w = fogFar
    uvec4 uFogDims;      // x = nx, y = ny, z = nz, w = featureMask
    uvec4 uHzbParams;    // x = width, y = height, z = mipCount, w = prevHzbValid
    uvec4 uPointShadowMeta[4]; // x = lightIdx (0xFFFFFFFFu = none), y = nearBits, z = farBits, w = fadeBits
};

vec3 reconstructWorldPos(vec2 screenUV, float reversedZDepth)
{
    vec4 ndc = vec4(screenUV * 2.0 - 1.0, reversedZDepth, 1.0);
    vec4 worldH = uInvViewProj * ndc;
    return worldH.xyz / worldH.w;
}
