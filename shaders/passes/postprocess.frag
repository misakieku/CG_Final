#version 460 core
in vec2 vUV;

#include "common/frame_ubo.glsl"
#include "common/tonemap.glsl"

layout(binding = 0) uniform sampler2D uHdrTex;
layout(binding = 1) uniform sampler2D uDepthTex; // Reversed-Z [0, 1]

out vec4 FragColor;

void main()
{
    vec3 hdr = texture(uHdrTex, vUV).rgb;
    float depth = texture(uDepthTex, vUV).r;

    vec3 color = hdr;
    if (depth > 0.0)
    {
        vec3 worldPos = reconstructWorldPos(vUV, depth);
        float dist = length(uCameraPos.xyz - worldPos);
        float fogFactor = 1.0 - exp(-dist * 0.038);
        color = mix(hdr, kNightSkyColor, clamp(fogFactor, 0.0, 1.0));
    }

    FragColor = vec4(linearToSrgb(ACESFilm(color)), 1.0);
}
