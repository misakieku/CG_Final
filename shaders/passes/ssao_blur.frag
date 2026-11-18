#version 460 core
in vec2 vUV;

#include "common/frame_ubo.glsl"

layout(binding = 0) uniform sampler2D uAoRawTex;
layout(binding = 1) uniform sampler2D uDepthTex;

out float FragAO;

float reconstructViewZ(vec2 uv)
{
    float d = texture(uDepthTex, uv).r;
    if (d <= 0.0)
    {
        return uTimeParams.w;
    }
    vec3 wp = reconstructWorldPos(uv, d);
    return -(uView * vec4(wp, 1.0)).z;
}

void main()
{
    float centerDepth = texture(uDepthTex, vUV).r;
    if (centerDepth <= 0.0)
    {
        FragAO = 1.0;
        return;
    }

    float centerZ = reconstructViewZ(vUV);
    vec2 invAoSize = 1.0 / vec2(textureSize(uAoRawTex, 0));

    float sumAo = 0.0;
    float sumWeight = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 tapUV = clamp(vUV + vec2(float(x), float(y)) * invAoSize, vec2(0.001), vec2(0.999));
            float tapAo = texture(uAoRawTex, tapUV).r;
            float tapZ = reconstructViewZ(tapUV);
            float spatialW = (x == 0 && y == 0) ? 0.25 : ((abs(x) + abs(y) == 1) ? 0.125 : 0.0625);
            float depthW = exp(-abs(centerZ - tapZ) * 4.5);
            float w = spatialW * depthW;
            sumAo += tapAo * w;
            sumWeight += w;
        }
    }

    FragAO = (sumWeight > 1e-5) ? (sumAo / sumWeight) : 1.0;
}
