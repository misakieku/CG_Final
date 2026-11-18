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
        color = hdr * fog.a + fog.rgb;
    }

    FragColor = vec4(linearToSrgb(ACESFilm(color)), 1.0);
}
