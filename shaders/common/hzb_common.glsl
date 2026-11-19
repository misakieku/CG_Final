#pragma once

bool isAabbOccludedByHzb(vec3 localMin, vec3 localMax, mat4 model, mat4 vp, sampler2D hzbTex, uvec3 hzbInfo)
{
    mat4 mvp = vp * model;
    vec2 uvMin = vec2(1.0);
    vec2 uvMax = vec2(0.0);
    float maxZ = 0.0;

    for (int i = 0; i < 8; ++i)
    {
        vec3 corner = vec3(
            ((i & 1) != 0) ? localMax.x : localMin.x,
            ((i & 2) != 0) ? localMax.y : localMin.y,
            ((i & 4) != 0) ? localMax.z : localMin.z
        );
        vec4 clip = mvp * vec4(corner, 1.0);
        if (clip.w <= 1e-4 || clip.z > clip.w)
        {
            return false;
        }
        vec3 ndc = clip.xyz / clip.w;
        vec2 uv = ndc.xy * 0.5 + 0.5;
        uvMin = min(uvMin, uv);
        uvMax = max(uvMax, uv);
        maxZ = max(maxZ, ndc.z);
    }

    uvMin = clamp(uvMin, 0.0, 1.0);
    uvMax = clamp(uvMax, 0.0, 1.0);
    if (uvMax.x <= uvMin.x || uvMax.y <= uvMin.y)
    {
        return false;
    }

    vec2 sizeTexels = (uvMax - uvMin) * vec2(hzbInfo.xy);
    float maxDim = max(sizeTexels.x, sizeTexels.y);
    int maxMip = max(int(hzbInfo.z) - 1, 0);
    int mip = clamp(int(ceil(log2(max(maxDim, 1.0)))), 0, maxMip);

    ivec2 mipSize = max(ivec2(1), ivec2(hzbInfo.xy) >> mip);
    ivec2 p0 = clamp(ivec2(floor(uvMin * vec2(mipSize))), ivec2(0), mipSize - ivec2(1));
    ivec2 p1 = clamp(ivec2(floor(uvMax * vec2(mipSize))), ivec2(0), mipSize - ivec2(1));

    float d00 = texelFetch(hzbTex, ivec2(p0.x, p0.y), mip).r;
    float d10 = texelFetch(hzbTex, ivec2(p1.x, p0.y), mip).r;
    float d01 = texelFetch(hzbTex, ivec2(p0.x, p1.y), mip).r;
    float d11 = texelFetch(hzbTex, ivec2(p1.x, p1.y), mip).r;
    float hzbMinDepth = min(min(d00, d10), min(d01, d11));

    return maxZ < (hzbMinDepth - 1e-5);
}
