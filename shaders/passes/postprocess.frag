#version 460 core
in vec2 vUV;

#include "common/frame_ubo.glsl"
#include "common/tonemap.glsl"
#include "common/fog_common.glsl"

layout(binding = 0) uniform sampler2D uHdrTex;
layout(binding = 1) uniform sampler2D uDepthTex; // Reversed-Z [0, 1]
layout(binding = 2) uniform sampler3D uFogAccumTex;

out vec4 FragColor;

void main()
{
    vec3 hdr = texture(uHdrTex, vUV).rgb;
    float depth = texture(uDepthTex, vUV).r;

    vec3 color = hdr;
    if (uFogParams.x > 1e-5)
    {
        float nearZ = max(uTimeParams.z, 0.05);
        float fogFar = max(uFogParams.w, nearZ + 1.0);
        float linearZ = fogFar;
        if (depth > 0.0)
        {
            vec3 worldPos = reconstructWorldPos(vUV, depth);
            vec3 viewPos = (uView * vec4(worldPos, 1.0)).xyz;
            linearZ = max(-viewPos.z, nearZ);
        }
        vec3 uvw = fogLinearZToUVW(vUV, linearZ, nearZ, fogFar, uFogDims.z);
        vec4 fog = texture(uFogAccumTex, uvw);
        if (depth <= 0.0)
        {
            // Keep horizon mist seamless against parapets while clearing upper night sky for moon, clouds, and stars
            vec3 skyDir = normalize(reconstructWorldPos(vUV, 0.5) - uCameraPos.xyz);
            float skyFogWeight = mix(1.0, 0.18, smoothstep(0.04, 0.46, max(skyDir.y, 0.0)));
            fog.rgb *= skyFogWeight;
            fog.a = mix(1.0, fog.a, skyFogWeight);
        }
        color = hdr * fog.a + fog.rgb;
    }

    vec3 srgb = linearToSrgb(ACESFilm(color));
    float luma = dot(srgb, vec3(0.299, 0.587, 0.114));
    FragColor = vec4(srgb, luma);
}
