#version 460 core
in vec2 vUV;

#include "common/frame_ubo.glsl"
#include "common/fog_common.glsl"

layout(binding = 0) uniform sampler2D uHdrTex;
layout(binding = 1) uniform sampler2D uDepthTex;
layout(binding = 2) uniform sampler3D uFogAccumTex;

uniform vec2 uSrcTexelSize;
uniform float uThreshold = 1.05;
uniform float uKnee = 0.55;

out vec4 FragColor;

void main()
{
    float depth = texture(uDepthTex, vUV).r;
    vec4 fog = sampleVolumetricFogAtPixel(vUV, depth, uFogAccumTex);

    // 4-tap bilinear box covering the 2x2 high-res footprint with Karis weighting against subpixel fireflies
    const vec2 kOffsets[4] = vec2[4](
        vec2(-0.5, -0.5),
        vec2( 0.5, -0.5),
        vec2(-0.5,  0.5),
        vec2( 0.5,  0.5)
    );

    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        vec3 s = max(texture(uHdrTex, vUV + kOffsets[i] * uSrcTexelSize).rgb, vec3(0.0));
        s = max(s * fog.a + fog.rgb, vec3(0.0));
        float luma = max(s.r, max(s.g, s.b));
        float kw = 1.0 / (1.0 + luma);
        sum += s * kw;
        weightSum += kw;
    }
    vec3 color = sum / max(weightSum, 1e-5);

    float br = max(color.r, max(color.g, color.b));
    float rq = clamp(br - uThreshold + uKnee, 0.0, 2.0 * uKnee);
    rq = (rq * rq) / max(4.0 * uKnee, 1e-5);
    float softWeight = max(rq, br - uThreshold) / max(br, 1e-5);
    vec3 bloom = min(color * softWeight, vec3(12.0));
    FragColor = vec4(bloom, 1.0);
}
