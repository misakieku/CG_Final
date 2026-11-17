#pragma once

uint Hash1D(uint x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float Hash01(uint x)
{
    return float(Hash1D(x)) * (1.0 / 4294967295.0);
}

vec2 Hash22(ivec2 cell, ivec2 period)
{
    ivec2 c = ((cell % period) + period) % period;
    uint h = uint(c.x) * 374761393u + uint(c.y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    return vec2(float(h & 0xFFFFu), float(h >> 16)) / 65535.0;
}

vec2 HashGrad2D(ivec2 cell, ivec2 period)
{
    ivec2 c = ((cell % period) + period) % period;

    uint h = uint(c.x) * 374761393u + uint(c.y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);

    float angle = float(h) * (6.28318530718 / 4294967296.0);
    return vec2(cos(angle), sin(angle));
}

vec4 Hash24(ivec2 cell, ivec2 period)
{
    ivec2 c = ((cell % period) + period) % period;
    uint h1 = uint(c.x) * 374761393u + uint(c.y) * 668265263u;
    h1 = (h1 ^ (h1 >> 13)) * 1274126177u;
    h1 ^= (h1 >> 16);

    uint h2 = uint(c.x) * 2246822519u + uint(c.y) * 3266489917u;
    h2 = (h2 ^ (h2 >> 15)) * 668265263u;
    h2 ^= (h2 >> 16);

    return vec4(float(h1 & 0xFFFFu), float(h1 >> 16), float(h2 & 0xFFFFu), float(h2 >> 16)) / 65535.0;
}

float BoxSDF(vec2 p, vec2 halfSize)
{
    vec2 d = abs(p) - halfSize;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float GradientNoise2D(vec2 p, ivec2 period)
{
    vec2 i = floor(p);
    vec2 f = fract(p);

    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    ivec2 i0 = ivec2(i);
    vec2 g00 = HashGrad2D(i0 + ivec2(0, 0), period);
    vec2 g10 = HashGrad2D(i0 + ivec2(1, 0), period);
    vec2 g01 = HashGrad2D(i0 + ivec2(0, 1), period);
    vec2 g11 = HashGrad2D(i0 + ivec2(1, 1), period);

    float v00 = dot(g00, f - vec2(0.0, 0.0));
    float v10 = dot(g10, f - vec2(1.0, 0.0));
    float v01 = dot(g01, f - vec2(0.0, 1.0));
    float v11 = dot(g11, f - vec2(1.0, 1.0));

    float nx0 = mix(v00, v10, u.x);
    float nx1 = mix(v01, v11, u.x);
    return mix(nx0, nx1, u.y) * 1.41421356;
}

float FbmScalar2D(vec2 uv, int baseFreq, int octaves)
{
    float sum = 0.0;
    float amp = 0.5;
    float norm = 0.0;
    int freq = baseFreq;

    for (int i = 0; i < octaves; ++i)
    {
        sum += amp * GradientNoise2D(uv * float(freq), ivec2(freq, freq));
        norm += amp;
        freq *= 2;
        amp *= 0.5;
    }
    return sum / max(norm, 1e-6);
}

vec2 Fbm2D(vec2 uv, int baseFreq, int octaves)
{
    vec2 sum = vec2(0.0, 0.0);
    float amp = 0.5;
    float norm = 0.0;
    int freq = baseFreq;

    for (int i = 0; i < octaves; ++i)
    {
        ivec2 period = ivec2(freq, freq);
        vec2 p = uv * float(freq);

        float nx = GradientNoise2D(p, period);
        float ny = GradientNoise2D(p + vec2(17.0, 31.0), period);

        sum += amp * vec2(nx, ny);
        norm += amp;
        freq *= 2;
        amp *= 0.5;
    }
    return sum / max(norm, 1e-6);
}

vec2 GetVoronoiCellVector(vec2 uv, ivec2 gridCount, float edgeSoftness)
{
    vec2 p = uv * vec2(gridCount);
    ivec2 n = ivec2(floor(p));
    vec2 f = fract(p);

    float f1 = 8.0;
    float f2 = 8.0;
    vec2 v1 = vec2(0.0, 0.0);
    vec2 v2 = vec2(0.0, 0.0);

    for (int j = -1; j <= 1; ++j)
    {
        for (int i = -1; i <= 1; ++i)
        {
            ivec2 g = ivec2(i, j);
            ivec2 cellCoord = n + g;

            vec2 featurePos = Hash22(cellCoord, gridCount);
            vec2 r = vec2(g) + featurePos - f;
            float d2 = dot(r, r);

            vec2 cellVec = Hash22(cellCoord + ivec2(37, 91), gridCount) * 2.0 - 1.0;

            if (d2 < f1)
            {
                f2 = f1;
                v2 = v1;
                f1 = d2;
                v1 = cellVec;
            }
            else if (d2 < f2)
            {
                f2 = d2;
                v2 = cellVec;
            }
        }
    }

    if (edgeSoftness <= 0.0)
    {
        return v1;
    }

    float borderDist = sqrt(f2) - sqrt(f1);
    float t = smoothstep(0.0, edgeSoftness, borderDist);
    return mix(0.5 * (v1 + v2), v1, t);
}

float smin(float a, float b, float k)
{
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

// smoothK = 0.0 hard edges between pits; smoothK = 0.15 ~ 0.30 for pure smooth undulations
float SmoothVoronoiF1(vec2 uv, ivec2 gridCount, float smoothK)
{
    vec2 p = uv * vec2(gridCount);
    ivec2 n = ivec2(floor(p));
    vec2 f = fract(p);

    float d = 8.0;
    for (int j = -1; j <= 1; ++j)
    {
        for (int i = -1; i <= 1; ++i)
        {
            ivec2 g = ivec2(i, j);
            vec2 rnd = Hash22(n + g, gridCount);
            vec2 r = vec2(g) + rnd - f;
            float distSq = dot(r, r);

            d = (smoothK > 0.0) ? smin(d, distSq, smoothK) : min(d, distSq);
        }
    }
    return smoothstep(0.0, 0.85, d);
}

float SoftTerrace(float h, float steps, float steepness)
{
    float scaled = h * steps;
    float baseStep = floor(scaled);
    float f = fract(scaled);

    float edge = smoothstep(0.5 - steepness, 0.5 + steepness, f);
    return (baseStep + mix(f, edge, 0.75)) / steps;
}

vec2 VoronoiNoiseRandomVector(vec2 uv, float offset)
{
    // HLSL mul(uv, rowMajorMat2(15.27, 47.63, 99.41, 89.98)) == mat2(...) * uv in column-major GLSL
    mat2 m = mat2(15.27, 47.63, 99.41, 89.98);
    vec2 p = fract(sin(m * uv) * 46839.32);
    return vec2(sin(p.y * offset) * 0.5 + 0.5, cos(p.x * offset) * 0.5 + 0.5);
}

void Voronoi(vec2 uv, float angleOffset, float cellDensity, out float outDist, out float outCells)
{
    vec2 g = floor(uv * cellDensity);
    vec2 f = fract(uv * cellDensity);
    vec3 res = vec3(8.0, 0.0, 0.0);

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 lattice = vec2(float(x), float(y));
            vec2 offset = VoronoiNoiseRandomVector(lattice + g, angleOffset);
            float d = distance(lattice + offset, f);
            if (d < res.x)
            {
                res = vec3(d, offset.x, offset.y);
            }
        }
    }
    outDist = res.x;
    outCells = res.y;
}

float NoiseRandomValue(vec2 uv)
{
    return fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453);
}

float Interpolate(float a, float b, float t)
{
    return (1.0 - t) * a + (t * b);
}

float ValueNoise(vec2 uv)
{
    vec2 i = floor(uv);
    vec2 f = fract(uv);
    f = f * f * (3.0 - 2.0 * f);

    uv = abs(fract(uv) - 0.5);
    vec2 c0 = i + vec2(0.0, 0.0);
    vec2 c1 = i + vec2(1.0, 0.0);
    vec2 c2 = i + vec2(0.0, 1.0);
    vec2 c3 = i + vec2(1.0, 1.0);
    float r0 = NoiseRandomValue(c0);
    float r1 = NoiseRandomValue(c1);
    float r2 = NoiseRandomValue(c2);
    float r3 = NoiseRandomValue(c3);

    float bottomOfGrid = Interpolate(r0, r1, f.x);
    float topOfGrid = Interpolate(r2, r3, f.x);
    float t = Interpolate(bottomOfGrid, topOfGrid, f.y);
    return t;
}

float SimpleNoise(vec2 UV, float Scale)
{
    float t = 0.0;

    float freq = pow(2.0, float(0));
    float amp = pow(0.5, float(3-0));
    t += ValueNoise(vec2(UV.x*Scale/freq, UV.y*Scale/freq))*amp;

    freq = pow(2.0, float(1));
    amp = pow(0.5, float(3-1));
    t += ValueNoise(vec2(UV.x*Scale/freq, UV.y*Scale/freq))*amp;

    freq = pow(2.0, float(2));
    amp = pow(0.5, float(3-2));
    t += ValueNoise(vec2(UV.x*Scale/freq, UV.y*Scale/freq))*amp;

    return t;
}

float Blend_Exclusion(float baseVal, float blendVal, float opacity)
{
    float blended = blendVal + baseVal - (2.0 * blendVal * baseVal);
    return mix(baseVal, blended, opacity);
}

vec4 Blend_Exclusion(vec4 baseCol, vec4 blendCol, float opacity)
{
    vec4 blended = blendCol + baseCol - (2.0 * blendCol * baseCol);
    return mix(baseCol, blended, opacity);
}
