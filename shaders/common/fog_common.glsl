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

vec4 sampleVolumetricFogAtPixel(vec2 uv, float depth, sampler3D fogAccumTex)
{
    if (uFogParams.x <= 1e-5)
    {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }

    float nearZ = max(uTimeParams.z, 0.05);
    float fogFar = max(uFogParams.w, nearZ + 1.0);
    float linearZ = fogFar;
    if (depth > 0.0)
    {
        vec3 worldPos = reconstructWorldPos(uv, depth);
        vec3 viewPos = (uView * vec4(worldPos, 1.0)).xyz;
        linearZ = max(-viewPos.z, nearZ);
    }
    vec3 uvw = fogLinearZToUVW(uv, linearZ, nearZ, fogFar, uFogDims.z);
    vec4 fog = texture(fogAccumTex, uvw);
    if (depth <= 0.0)
    {
        // Keep horizon mist seamless against parapets while clearing upper night sky for moon, clouds, and stars
        vec3 skyDir = normalize(reconstructWorldPos(uv, 0.5) - uCameraPos.xyz);
        float skyFogWeight = mix(1.0, 0.18, smoothstep(0.04, 0.46, max(skyDir.y, 0.0)));
        fog.rgb *= skyFogWeight;
        fog.a = mix(1.0, fog.a, skyFogWeight);
    }
    return fog;
}
