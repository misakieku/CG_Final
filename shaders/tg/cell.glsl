#pragma once

#include "tg/common.glsl"

struct CellInfo
{
    vec2 bMin;
    vec2 bMax;
    float edgeDist; // physical distance to nearest mortar seam
    uint seed;      // per-stone independent random seed
};

CellInfo SubdivideStoneGrid(vec2 uv, uint baseSeed)
{
    vec2 macroChip = GetVoronoiCellVector(uv, ivec2(14, 14), 0.05);
    vec2 microChip = GetVoronoiCellVector(uv, ivec2(42, 42), 0.04);

    vec2 rockUV = uv + Fbm2D(uv, 2, 1) * 0.015 + macroChip * 0.014 + microChip * 0.005;

    vec2 bMin = vec2(0.0, 0.0);
    vec2 bMax = vec2(1.0, 1.0);
    uint seed = baseSeed;

    for (int depth = 0; depth < 5; ++depth)
    {
        vec2 size = bMax - bMin;

        // when depth >= 2, stop subdividing with probability 0.35 to keep larger stones
        if (depth >= 2 && Hash01(seed ^ 0x1234u) < 0.35)
        {
            break;
        }

        // Tend to split the longer edge to avoid thin needle-like stones
        bool splitX = (size.x > size.y)
                    ? (Hash01(seed ^ 0x5678u) < 0.85)
                    : (Hash01(seed ^ 0x5678u) < 0.15);

        float t = mix(0.32, 0.68, Hash01(seed ^ 0x9abcu));

        if (splitX)
        {
            float splitPos = mix(bMin.x, bMax.x, t);
            if (rockUV.x < splitPos) { bMax.x = splitPos; seed = Hash1D(seed * 2u + 0u); }
            else                     { bMin.x = splitPos; seed = Hash1D(seed * 2u + 1u); }
        }
        else
        {
            float splitPos = mix(bMin.y, bMax.y, t);
            if (rockUV.y < splitPos) { bMax.y = splitPos; seed = Hash1D(seed * 2u + 2u); }
            else                     { bMin.y = splitPos; seed = Hash1D(seed * 2u + 3u); }
        }
    }

    vec2 d = min(rockUV - bMin, bMax - rockUV);

    CellInfo info;
    info.bMin = bMin;
    info.bMax = bMax;
    info.edgeDist = min(d.x, d.y);
    info.seed = seed;
    return info;
}

// power = 2.0 is rounded stones, power = 3.5 ~ 4.5 is irregular polygonal blocks, jitter in [0.6, 0.85]
CellInfo VoronoiStoneGrid(vec2 uv, ivec2 gridCount, float power, float jitter)
{
    vec2 p = (uv + Fbm2D(uv, 4, 3) * 0.02) * vec2(gridCount);
    ivec2 n = ivec2(floor(p));
    vec2 f = fract(p);

    float f1 = 8.0;
    float f2 = 8.0;
    uint bestSeed = 0u;

    for (int j = -1; j <= 1; ++j)
    {
        for (int i = -1; i <= 1; ++i)
        {
            ivec2 g = ivec2(i, j);
            vec2 rnd = Hash22(n + g, gridCount);
            vec2 r = vec2(g) + 0.5 + (rnd - 0.5) * jitter - f;

            // Minkowski distance: (|x|^p + |y|^p)^(1/p), guarded against pow(0, p) NaN
            vec2 ar = max(abs(r), vec2(1e-6));
            float d = pow(pow(ar.x, power) + pow(ar.y, power), 1.0 / power);

            if (d < f1)
            {
                f2 = f1;
                f1 = d;
                ivec2 wrappedCell = ((n + g) % gridCount + gridCount) % gridCount;
                bestSeed = uint(wrappedCell.x * 73856093 ^ wrappedCell.y * 19349663);
            }
            else if (d < f2)
            {
                f2 = d;
            }
        }
    }

    CellInfo info;
    info.bMin = vec2(0.0);
    info.bMax = vec2(1.0);
    info.edgeDist = (f2 - f1) / float(gridCount.x);
    info.seed = Hash1D(bestSeed);
    return info;
}

CellInfo ScatteredStoneGrid(vec2 uv, ivec2 gridCount, uint baseSeed)
{
    vec2 warpedUV = uv + Fbm2D(uv, 5, 3) * 0.015;
    vec2 p = warpedUV * vec2(gridCount);
    ivec2 n = ivec2(floor(p));
    vec2 f = fract(p);

    float s1 = -10.0;
    float s2 = -10.0;
    uint bestSeed = 0u;

    for (int j = -1; j <= 1; ++j)
    {
        for (int i = -1; i <= 1; ++i)
        {
            ivec2 g = ivec2(i, j);
            ivec2 cellCoord = n + g;
            vec4 rnd = Hash24(cellCoord, gridCount);

            vec2 center = vec2(g) + 0.5 + (rnd.xy - 0.5) * 0.45;
            vec2 halfSize = mix(vec2(0.50, 0.50), vec2(0.82, 0.82), rnd.zw);

            ivec2 wrapped = ((cellCoord % gridCount) + gridCount) % gridCount;
            uint seed = Hash1D(uint(wrapped.x * 73856093 ^ wrapped.y * 19349663) + baseSeed);
            float slope = mix(0.75, 1.35, Hash01(seed));

            float sdf = BoxSDF(f - center, halfSize);
            float score = -sdf * slope;

            if (score > s1)
            {
                s2 = s1;
                s1 = score;
                bestSeed = seed;
            }
            else if (score > s2)
            {
                s2 = score;
            }
        }
    }

    CellInfo info;
    info.bMin = vec2(0.0);
    info.bMax = vec2(1.0);
    info.edgeDist = (s1 > 0.0) ? min(s1, s1 - s2) : 0.0;
    info.edgeDist /= float(gridCount.x);
    info.seed = bestSeed;
    return info;
}

float ComputeStoneHeight(vec2 uv, float edgeDist, uint seed)
{
    float macroErosion = FbmScalar2D(uv, 16, 3) * 0.5 + 0.5;
    float microFacets = SmoothVoronoiF1(uv, ivec2(48, 48), 0.0);
    float erosionNoise = macroErosion * 0.65 + microFacets * 0.35;
    float erodedEdgeDist = edgeDist - erosionNoise * 0.02;

    float slopeVar = FbmScalar2D(uv + vec2(13.0, 7.0), 10, 2) * 0.5 + 0.5;
    float bevelWidth = mix(0.006, 0.032, slopeVar);
    float bevel = smoothstep(0.0, bevelWidth, erodedEdgeDist);

    float blockBase = mix(0.65, 1.0, Hash01(seed));
    float angle = Hash01(seed ^ 0x4567u) * 6.2831853;
    float tilt = sin(uv.x * 6.2831853 * 8.0 + angle) * 0.04;

    vec2 warpedUV = uv + Fbm2D(uv, 3, 2) * 0.06;
    float broadWave = FbmScalar2D(warpedUV, 4, 2) * 0.5 + 0.5;
    float cleavage = SoftTerrace(broadWave, 5.0, 0.30);
    float gouges = SmoothVoronoiF1(warpedUV, ivec2(8, 8), 0.22);

    float surfaceRelief = (cleavage * 0.6 + gouges * 0.4 - 0.5) * 0.22 + tilt;

    float shoulderMask = 4.0 * bevel * (1.0 - bevel);
    float rimNoise = (FbmScalar2D(uv, 64, 2) * 0.5 + 0.5) * shoulderMask * 0.08;

    float stoneHeight = bevel * (blockBase + surfaceRelief) - rimNoise;
    float mortarHeight = (FbmScalar2D(uv, 24, 3) * 0.5 + 0.5) * 0.12;

    return max(stoneHeight, mortarHeight);
}

float RidgedMultifractal2D(vec2 uv, int baseFreq, int octaves, float sharpness, float gain)
{
    float sum = 0.0;
    float amp = 0.5;
    float weight = 1.0;
    float norm = 0.0;
    int freq = baseFreq;

    for (int i = 0; i < octaves; ++i)
    {
        float n = GradientNoise2D(uv * float(freq), ivec2(freq, freq));
        float signal = clamp(1.0 - abs(n), 1e-6, 1.0);
        signal = pow(signal, sharpness);
        signal *= weight;
        weight = clamp(signal * gain, 0.0, 1.0);

        sum += signal * amp;
        norm += amp;
        freq *= 2;
        amp *= 0.5;
    }
    return sum / max(norm, 1e-6);
}

void MaskedRidgedMultifractal2D(vec2 uv, float baseFreq, float octaves, float sharpness, float gain, out float value, out float mask)
{
    vec2 crackUV = uv + Fbm2D(uv, 5, 3) * 0.04;
    value = RidgedMultifractal2D(crackUV, int(baseFreq), int(octaves), sharpness, gain);
    mask = smoothstep(0.45, 0.75, FbmScalar2D(uv, max(1, int(baseFreq * 0.5)), 2) * 0.5 + 0.5);
}

float ModifyStoneHeight(vec2 uv, float finalCrack, float crackMask, float stoneHeight)
{
    vec2 crackUV = uv + Fbm2D(uv, 5, 3) * 0.04;
    float n0 = GradientNoise2D(crackUV * 4.0, ivec2(4, 4));
    float a0 = clamp(1.0 - abs(n0), 1e-6, 1.0);
    float shearStep = (smoothstep(-0.08, 0.08, n0) - 0.5) * (a0 * a0);
    return stoneHeight - finalCrack * 0.18 + shearStep * crackMask * 0.06;
}
