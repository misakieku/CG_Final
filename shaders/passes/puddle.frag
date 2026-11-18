#version 460 core
in vec2 vUV;

#include "common/frame_ubo.glsl"
#include "common/gbuffer_common.glsl"

layout(binding = 0) uniform sampler2D uGBuffer0;
layout(binding = 1) uniform sampler2D uGBuffer1;
layout(binding = 2) uniform sampler2D uDepthTex;

layout(location = 0) out vec4 outGBuffer0;
layout(location = 1) out vec4 outGBuffer1;

float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash21(i + vec2(0.0, 0.0));
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbmPuddle(vec2 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; ++i)
    {
        v += a * valueNoise(p);
        p = p * 2.03 + vec2(17.1, 31.7);
        a *= 0.5;
    }
    return v;
}

void main()
{
    if (uGiMisc.w > 0.0)
    {
        discard;
    }

    ivec2 coord = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(uDepthTex, coord, 0).r;
    if (depth <= 0.0)
    {
        discard;
    }

    vec4 g0 = texelFetch(uGBuffer0, coord, 0);
    vec4 g1 = texelFetch(uGBuffer1, coord, 0);

    // Modify ONLY base mud floor pixels (kMatFloor = 0), keeping flagstones, walls, vines, planks untouched
    uint matId = uint(round(g1.b));
    if (matId != 0u)
    {
        discard;
    }

    vec3 worldPos = reconstructWorldPos(vUV, depth);
    vec2 warp = vec2(fbmPuddle(worldPos.xz * 0.28), fbmPuddle(worldPos.xz * 0.28 + vec2(9.2, 4.7)));
    float basin = fbmPuddle(worldPos.xz * 0.38 + (warp - 0.5) * 0.75);

    float wetMask = smoothstep(0.50, 0.59, basin);
    if (wetMask <= 0.002)
    {
        discard;
    }
    float waterMask = smoothstep(0.57, 0.65, basin);

    vec3 N = octDecodeNormal(g1.rg);
    float rough = abs(g1.a);

    // Damp dark soil darkening around rims and beneath standing puddles
    vec3 wetAlbedo = g0.rgb * mix(1.0, 0.42, wetMask * 0.78 + waterMask * 0.22);

    // Animated micro wind ripples across flat standing puddle water
    float t = uTimeParams.x * 1.4;
    vec2 rp = worldPos.xz * 5.2;
    float rx = (valueNoise(rp + vec2(t * 0.55, 0.0)) - valueNoise(rp - vec2(t * 0.55, 0.0))) * 0.032;
    float rz = (valueNoise(rp + vec2(0.0, -t * 0.45) + 7.3) - valueNoise(rp - vec2(0.0, -t * 0.45) + 7.3)) * 0.032;
    vec3 waterN = normalize(vec3(rx * waterMask, 1.0, rz * waterMask));

    vec3 outN = normalize(mix(N, waterN, clamp(wetMask * 0.45 + waterMask * 0.95, 0.0, 1.0)));
    float outRough = clamp(mix(rough, mix(0.24, 0.04, waterMask), wetMask), 0.04, 1.0);

    outGBuffer0 = vec4(wetAlbedo, g0.a);
    outGBuffer1 = vec4(octEncodeNormal(outN), g1.b, (g1.a < 0.0) ? -outRough : outRough);
}
