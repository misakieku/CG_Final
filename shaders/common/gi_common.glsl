#pragma once

#define GI_RAY_COUNT 32
#if GI_RAY_COUNT <= 16
#define GI_PROBES_PER_GROUP 4
#elif GI_RAY_COUNT <= 32
#define GI_PROBES_PER_GROUP 2
#else
#define GI_PROBES_PER_GROUP 1
#endif

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

float skyHash31(vec3 p)
{
    p = fract(p * vec3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

float skyHash21(vec2 p)
{
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 34.5);
    return fract(p.x * p.y);
}

float skyNoise2D(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(skyHash21(i), skyHash21(i + vec2(1.0, 0.0)), u.x),
        mix(skyHash21(i + vec2(0.0, 1.0)), skyHash21(i + vec2(1.0, 1.0)), u.x),
        u.y
    );
}

float skyFbm(vec2 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; ++i)
    {
        v += a * skyNoise2D(p);
        p = p * 2.07 + vec2(13.4, 29.1);
        a *= 0.5;
    }
    return v;
}

vec3 evalNightSkyDetailed(vec3 dir, float time)
{
    dir = normalize(dir);
    float up = clamp(dir.y, 0.0, 1.0);

    // Atmosphere gradient + subtle diagonal Milky Way / twilight veil
    vec3 horizonCol = vec3(0.048, 0.075, 0.145);
    vec3 zenithCol = vec3(0.006, 0.010, 0.026);
    vec3 sky = mix(horizonCol, zenithCol, pow(up, 0.55));

    float galacticCoord = dir.x * 0.65 - dir.z * 0.76 + (skyFbm(dir.xz * 2.4) - 0.5) * 0.35;
    float milkyWay = exp(-galacticCoord * galacticCoord * 7.5) * smoothstep(0.08, 0.55, up);
    sky += vec3(0.016, 0.028, 0.062) * milkyWay * (0.65 + 0.35 * skyFbm(dir.xy * 5.0));

    // Full moon aligned cleanly with kMoonDir + multi-band halo
    float moonDot = dot(dir, kMoonDir);
    float outerHalo = pow(max(moonDot, 0.0), 7.0);
    float innerHalo = pow(max(moonDot, 0.0), 42.0);
    sky += kMoonColor * (outerHalo * 0.30 + innerHalo * 0.55);

    float moonDisc = smoothstep(0.99855, 0.99895, moonDot);
    if (moonDisc > 0.001)
    {
        vec3 moonRight = normalize(cross(vec3(0.0, 1.0, 0.0), kMoonDir));
        vec3 moonUp = cross(kMoonDir, moonRight);
        vec2 discUV = vec2(dot(dir, moonRight), dot(dir, moonUp)) * 18.0;
        float maria = skyFbm(discUV * 1.4 + vec2(2.3, 5.1));
        vec3 moonSurface = mix(vec3(0.95, 1.02, 1.18), vec3(1.48, 1.56, 1.78), smoothstep(0.32, 0.68, maria));
        sky = mix(sky, moonSurface, moonDisc);
    }

    // Drifting high-altitude night clouds with moonlight silver-lining
    float cloudMask = 0.0;
    if (dir.y > 0.01)
    {
        vec2 cloudPlane = dir.xz / max(dir.y + 0.12, 0.14);
        vec2 wind = vec2(time * 0.010, time * 0.005);
        float cMain = skyFbm(cloudPlane * 0.52 + wind);
        float cDetail = skyFbm(cloudPlane * 1.25 - wind * 1.4 + vec2(4.7, 9.2));
        float rawCloud = cMain * 0.66 + cDetail * 0.34;
        cloudMask = smoothstep(0.41, 0.71, rawCloud) * smoothstep(0.01, 0.14, dir.y);

        float edgeHighlight = smoothstep(0.38, 0.56, rawCloud) * (1.0 - smoothstep(0.56, 0.78, rawCloud));
        float moonRim = pow(max(moonDot, 0.0), 4.0) * (0.45 + 1.15 * edgeHighlight) + pow(max(moonDot, 0.0), 20.0) * 0.65;
        vec3 darkCloud = vec3(0.014, 0.020, 0.042);
        vec3 litCloud = mix(vec3(0.065, 0.098, 0.185), vec3(0.45, 0.60, 0.92), clamp(moonRim, 0.0, 1.0));
        vec3 cloudCol = mix(darkCloud, litCloud, clamp(0.24 + moonRim * 0.85, 0.0, 1.0));
        sky = mix(sky, cloudCol, cloudMask * 0.86);
    }

    // Sparse twinkling stars peeking between cloud breaks and away from moon glare
    if (dir.y > 0.03)
    {
        vec3 starCell = floor(dir * 165.0);
        float h = skyHash31(starCell);
        if (h > 0.9915)
        {
            vec3 starDir = normalize(starCell + 0.3 + 0.4 * vec3(fract(h * 17.3), fract(h * 31.7), fract(h * 53.1)));
            float angDist = length(dir - starDir) * 165.0;
            float pinpoint = smoothstep(0.20, 0.03, angDist);
            float twinkle = 0.70 + 0.30 * sin(time * 1.9 + h * 62.83);
            float vis = smoothstep(0.03, 0.20, dir.y) * (1.0 - cloudMask * 0.92) * (1.0 - moonDisc) * (1.0 - innerHalo * 0.75);
            float mag = 0.25 + 1.15 * pow(fract(h * 41.7), 2.5);
            vec3 starTint = mix(vec3(0.75, 0.86, 1.0), vec3(1.0, 0.94, 0.82), fract(h * 97.1));
            sky += starTint * (pinpoint * twinkle * vis * mag);
        }
    }

    return sky;
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

    const int kSteps = 8;
    float stepLen = lenXZ / float(kSteps);
    float opticalDepth = 0.0;
    for (int i = 0; i < kSteps; ++i)
    {
        float t = (float(i) + 0.5) / float(kSteps);
        vec3 p = mix(surfPos, lightPos, t);
        opticalDepth += sampleWallCoreDensity(p.xz, p.y, mazeGrid) * stepLen;
        if (opticalDepth >= 1.35)
        {
            return 0.0;
        }
    }

    return smoothstep(1.35, 0.35, opticalDepth);
}
