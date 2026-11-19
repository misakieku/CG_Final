#version 460 core
in vec2 vUV;

#include "common/frame_ubo.glsl"
#include "common/tonemap.glsl"
#include "common/fog_common.glsl"

layout(binding = 0) uniform sampler2D uHdrTex;
layout(binding = 1) uniform sampler2D uDepthTex; // Reversed-Z [0, 1]
layout(binding = 2) uniform sampler3D uFogAccumTex;
layout(binding = 3) uniform sampler2D uBloomTex;

out vec4 FragColor;

void main()
{
    vec3 hdr = texture(uHdrTex, vUV).rgb;
    float depth = texture(uDepthTex, vUV).r;

    vec4 fog = ((uFogDims.w & kFeatureFogBit) != 0u)
        ? sampleVolumetricFogAtPixel(vUV, depth, uFogAccumTex)
        : vec4(0.0, 0.0, 0.0, 1.0);
    vec3 color = hdr * fog.a + fog.rgb;
    if ((uFogDims.w & kFeatureBloomBit) != 0u)
    {
        vec3 bloom = textureLod(uBloomTex, vUV, 0.0).rgb;
        color += bloom * 0.085;
    }

    vec3 srgb = linearToSrgb(ACESFilm(color));
    float luma = dot(srgb, vec3(0.299, 0.587, 0.114));
    FragColor = vec4(srgb, luma);
}
