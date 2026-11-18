#pragma once

const uint kGiCellWall = 0u;
const uint kGiCellRoofed = 1u;
const uint kGiCellCourtyard = 2u;

vec3 giProbeToWorld(ivec3 c)
{
    return uGiGridOrigin.xyz + vec3(c) / uGiGridInvStep.xyz;
}

uint giProbeIndex(ivec3 c)
{
    return (uint(c.z) * uGiDims.y + uint(c.y)) * uGiDims.x + uint(c.x);
}

ivec3 giCoordFromIndex(uint idx)
{
    uint nx = uGiDims.x;
    uint ny = uGiDims.y;
    uint ix = idx % nx;
    uint iy = (idx / nx) % ny;
    uint iz = idx / (nx * ny);
    return ivec3(int(ix), int(iy), int(iz));
}

vec3 evalNightSky(vec3 dir)
{
    float up = clamp(dir.y, 0.0, 1.0);
    vec3 zenith = kNightSkyColor;
    vec3 horizon = vec3(0.052, 0.082, 0.155);
    vec3 sky = mix(horizon, zenith, pow(up, 0.65));
    float moonGlow = pow(max(dot(dir, kMoonDir), 0.0), 8.0);
    return sky + kMoonColor * (moonGlow * 0.45);
}

float evalL1Channel(vec4 sh, vec3 N)
{
    float r = sh.x;
    vec3 l1 = sh.yzw;
    float len = length(l1);
    float scale = (len > 1e-5) ? min(1.0, (1.5 * r) / len) : 1.0;
    return max(0.0, r + (2.0 / 3.0) * dot(l1 * scale, N));
}

vec3 evalL1Rgb(vec4 sh0, vec4 sh1, vec4 sh2, vec3 N)
{
    return vec3(evalL1Channel(sh0, N), evalL1Channel(sh1, N), evalL1Channel(sh2, N));
}

bool isGiProbeValid(ivec3 pc, usampler2D mazeGrid, bool rejectAboveRoof)
{
    if (any(lessThan(pc, ivec3(0))) || any(greaterThanEqual(pc, ivec3(uGiDims.xyz))))
    {
        return false;
    }
    ivec2 cell = clamp(ivec2(pc.x >> 1, pc.z >> 1), ivec2(0), ivec2(int(uGiMisc.y) - 1, int(uGiMisc.z) - 1));
    uint kind = texelFetch(mazeGrid, cell, 0).r;
    if (kind == kGiCellWall)
    {
        return false;
    }
    if (pc.y == 4 && (rejectAboveRoof || kind != kGiCellCourtyard))
    {
        return false;
    }
    return true;
}

// Leak-proof trilinear L1 SH sampling: filters out wall interiors, roof crossings, and backface probes
vec3 sampleProbeGiFiltered(vec3 worldPos, vec3 N, sampler3D sh0, sampler3D sh1, sampler3D sh2, usampler2D mazeGrid)
{
    if (uGiDims.w == 0u)
    {
        return vec3(0.0);
    }

    vec3 samplePos = worldPos + N * 0.16;
    vec3 gridCoord = clamp((samplePos - uGiGridOrigin.xyz) * uGiGridInvStep.xyz, vec3(0.0), vec3(uGiDims.xyz - uvec3(1u)));
    ivec3 base = min(ivec3(floor(gridCoord)), ivec3(uGiDims.xyz) - ivec3(2));
    vec3 f = clamp(gridCoord - vec3(base), vec3(0.0), vec3(1.0));

    ivec2 sampleCell = clamp(ivec2(floor(samplePos.xz / uGiGridInvStep.w)), ivec2(0), ivec2(int(uGiMisc.y) - 1, int(uGiMisc.z) - 1));
    uint sampleCellKind = texelFetch(mazeGrid, sampleCell, 0).r;
    bool rejectAboveRoof = (sampleCellKind != kGiCellCourtyard) && (worldPos.y <= uGiMisc.x + 0.06);

    vec4 sum0 = vec4(0.0);
    vec4 sum1 = vec4(0.0);
    vec4 sum2 = vec4(0.0);
    float weightSum = 0.0;

    for (int oz = 0; oz <= 1; ++oz)
    {
        float wz = (oz == 0) ? (1.0 - f.z) : f.z;
        for (int oy = 0; oy <= 1; ++oy)
        {
            float wy = (oy == 0) ? (1.0 - f.y) : f.y;
            for (int ox = 0; ox <= 1; ++ox)
            {
                float wx = (ox == 0) ? (1.0 - f.x) : f.x;
                ivec3 pc = base + ivec3(ox, oy, oz);
                if (!isGiProbeValid(pc, mazeGrid, rejectAboveRoof))
                {
                    continue;
                }

                vec3 probePos = giProbeToWorld(pc);
                vec3 toProbe = probePos - worldPos;
                float dist = length(toProbe);
                vec3 dir = (dist > 1e-4) ? (toProbe / dist) : N;
                float dirWeight = smoothstep(-0.18, 0.30, dot(dir, N));
                float w = wx * wy * wz * (dirWeight * dirWeight + 0.02);

                sum0 += texelFetch(sh0, pc, 0) * w;
                sum1 += texelFetch(sh1, pc, 0) * w;
                sum2 += texelFetch(sh2, pc, 0) * w;
                weightSum += w;
            }
        }
    }

    if (weightSum <= 1e-6)
    {
        return vec3(0.0);
    }

    float invW = 1.0 / weightSum;
    return evalL1Rgb(sum0 * invW, sum1 * invW, sum2 * invW, N);
}

float sampleWallCoreDensity(vec2 xz, float y, usampler2D mazeGrid)
{
    float cellSize = uGiGridInvStep.w;
    ivec2 maxGrid = ivec2(int(uGiMisc.y) - 1, int(uGiMisc.z) - 1);
    vec2 g = xz / cellSize - 0.5;
    ivec2 i0 = ivec2(floor(g));
    vec2 f = clamp(g - vec2(i0), vec2(0.0), vec2(1.0));

    float w00 = (texelFetch(mazeGrid, clamp(i0 + ivec2(0, 0), ivec2(0), maxGrid), 0).r == kGiCellWall) ? 1.0 : 0.0;
    float w10 = (texelFetch(mazeGrid, clamp(i0 + ivec2(1, 0), ivec2(0), maxGrid), 0).r == kGiCellWall) ? 1.0 : 0.0;
    float w01 = (texelFetch(mazeGrid, clamp(i0 + ivec2(0, 1), ivec2(0), maxGrid), 0).r == kGiCellWall) ? 1.0 : 0.0;
    float w11 = (texelFetch(mazeGrid, clamp(i0 + ivec2(1, 1), ivec2(0), maxGrid), 0).r == kGiCellWall) ? 1.0 : 0.0;

    float raw = mix(mix(w00, w10, f.x), mix(w01, w11, f.x), f.y);
    float core = smoothstep(0.50, 0.78, raw);
    float hWeight = smoothstep(uGiMisc.x + 0.25, uGiMisc.x - 0.15, y);
    return core * hWeight;
}

float evalPointLightWallVisibility(vec3 surfPos, vec3 lightPos, usampler2D mazeGrid)
{
    vec2 d = lightPos.xz - surfPos.xz;
    float lenXZ = length(d);
    if (lenXZ < 0.15)
    {
        return 1.0;
    }

    const int kSteps = 14;
    float stepLen = lenXZ / float(kSteps);
    float opticalDepth = 0.0;
    for (int i = 0; i < kSteps; ++i)
    {
        float t = (float(i) + 0.5) / float(kSteps);
        vec3 p = mix(surfPos, lightPos, t);
        opticalDepth += sampleWallCoreDensity(p.xz, p.y, mazeGrid) * stepLen;
    }

    return smoothstep(1.35, 0.35, opticalDepth);
}
