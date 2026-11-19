#version 460 core
in vec2 vUV;
flat in vec4 vParams;

#include "common/frame_ubo.glsl"

out vec4 FragColor;

float flameHash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float flameNoise2D(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(flameHash21(i), flameHash21(i + vec2(1.0, 0.0)), u.x),
        mix(flameHash21(i + vec2(0.0, 1.0)), flameHash21(i + vec2(1.0, 1.0)), u.x),
        u.y
    );
}

float flameFbm3(vec2 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 3; ++i)
    {
        v += a * flameNoise2D(p);
        p = p * 2.03 + vec2(7.1, 13.7);
        a *= 0.5;
    }
    return v;
}

void main()
{
    float seed = vParams.y;
    float hdrIntensity = vParams.z;
    bool isTorch = (vParams.w > 0.5);
    float t = uTimeParams.x + seed * 17.31;

    if (!isTorch)
    {
        // Tiny dark wick anchor at candle crown
        if (vUV.y < 0.14 && abs(vUV.x) < 0.055)
        {
            FragColor = vec4(0.025, 0.018, 0.014, 0.92);
            return;
        }

        float fy = clamp((vUV.y - 0.09) / 0.78, 0.0, 1.0);
        float sway = sin(t * 4.3 + seed * 3.1 - fy * 3.2) * 0.065 * fy * fy;
        float profile = sin(pow(max(fy, 1e-4), 0.56) * 3.14159265) * (1.0 - 0.34 * fy);
        float dx = abs(vUV.x - sway) / max(profile * 0.42, 0.008);

        float density = (vUV.y >= 0.08 && vUV.y <= 0.88) ? clamp(1.0 - dx, 0.0, 1.0) : 0.0;
        float bodyMask = smoothstep(0.05, 0.35, density);
        float coreMask = smoothstep(0.45, 0.82, density) * (1.0 - smoothstep(0.55, 0.92, fy));

        vec3 outerCol = vec3(1.0, 0.42, 0.06) * (hdrIntensity * 0.82);
        vec3 coreCol = vec3(1.0, 0.88, 0.56) * (hdrIntensity * 1.35);
        vec3 fireCol = mix(outerCol, coreCol, coreMask);

        float blueBase = (1.0 - smoothstep(0.04, 0.26, fy)) * smoothstep(0.0, 0.08, fy) * bodyMask;
        fireCol = mix(fireCol, vec3(0.22, 0.52, 1.45) * (hdrIntensity * 0.45), blueBase * 0.65);

        vec2 haloDelta = vec2(vUV.x * 1.15, (vUV.y - 0.38) * 1.25);
        float halo = exp(-dot(haloDelta, haloDelta) * 8.0);
        vec3 haloCol = vec3(0.85, 0.28, 0.03) * (hdrIntensity * 0.14 * halo);

        float alpha = clamp(bodyMask * 0.90, 0.0, 0.90);
        FragColor = vec4(fireCol * alpha + haloCol, alpha);
        return;
    }

    // Roaring multi-tongue torch flame with upward scrolling FBM turbulence
    float fy = clamp(vUV.y / 0.96, 0.0, 1.0);
    float n = flameFbm3(vec2(vUV.x * 2.8 + seed * 3.7, vUV.y * 3.4 - t * 3.8));
    float n2 = flameFbm3(vec2(vUV.x * 5.2 - seed * 2.1, vUV.y * 5.6 - t * 6.2));
    float sway = (n - 0.5) * 0.34 * fy + sin(t * 7.8 + seed - fy * 4.4) * 0.06 * fy;
    float widthEnv = sin(pow(max(fy, 1e-4), 0.52) * 3.14159265) * (1.06 - 0.44 * fy) * (0.78 + 0.44 * n);
    float dx = abs(vUV.x - sway) / max(widthEnv * 0.56, 0.01);

    float tipErode = smoothstep(0.50, 0.98, fy) * (1.0 - n2) * 0.85;
    float density = clamp(1.0 - dx - tipErode, 0.0, 1.0) * smoothstep(0.0, 0.07, vUV.y);

    float bodyMask = smoothstep(0.06, 0.36, density);
    float midMask = smoothstep(0.14, 0.48, density);
    float coreMask = smoothstep(0.42, 0.78, density) * (1.0 - smoothstep(0.52, 0.88, fy));

    vec3 tipCol = vec3(1.45, 0.24, 0.02);
    vec3 midCol = vec3(1.0, 0.44, 0.06) * (hdrIntensity * 0.88);
    vec3 coreCol = vec3(1.0, 0.86, 0.50) * (hdrIntensity * 1.40);
    vec3 fireCol = mix(tipCol, midCol, midMask);
    fireCol = mix(fireCol, coreCol, coreMask);

    vec2 haloDelta = vec2(vUV.x, (vUV.y - 0.35) * 1.15);
    float halo = exp(-dot(haloDelta, haloDelta) * 5.2) * (0.88 + 0.12 * sin(t * 9.5 + seed));
    vec3 haloCol = vec3(0.95, 0.30, 0.03) * (hdrIntensity * 0.15 * halo);

    float alpha = clamp(bodyMask * 0.92, 0.0, 0.92);
    vec3 outRgb = fireCol * alpha + haloCol;
    if (alpha < 0.004 && dot(outRgb, outRgb) < 1e-5)
    {
        discard;
    }
    FragColor = vec4(outRgb, alpha);
}
