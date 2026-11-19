#version 460 core
in vec2 vUV;

layout(binding = 0) uniform sampler2D uBloomTex;

uniform vec2 uSrcTexelSize;
uniform float uFilterRadius = 1.15;

out vec4 FragColor;

void main()
{
    float x = uSrcTexelSize.x * uFilterRadius;
    float y = uSrcTexelSize.y * uFilterRadius;

    vec3 a = textureLod(uBloomTex, vec2(vUV.x - x, vUV.y + y), 0.0).rgb;
    vec3 b = textureLod(uBloomTex, vec2(vUV.x,     vUV.y + y), 0.0).rgb;
    vec3 c = textureLod(uBloomTex, vec2(vUV.x + x, vUV.y + y), 0.0).rgb;

    vec3 d = textureLod(uBloomTex, vec2(vUV.x - x, vUV.y),     0.0).rgb;
    vec3 e = textureLod(uBloomTex, vec2(vUV.x,     vUV.y),     0.0).rgb;
    vec3 f = textureLod(uBloomTex, vec2(vUV.x + x, vUV.y),     0.0).rgb;

    vec3 g = textureLod(uBloomTex, vec2(vUV.x - x, vUV.y - y), 0.0).rgb;
    vec3 h = textureLod(uBloomTex, vec2(vUV.x,     vUV.y - y), 0.0).rgb;
    vec3 i = textureLod(uBloomTex, vec2(vUV.x + x, vUV.y - y), 0.0).rgb;

    vec3 upsample = e * 4.0 + (b + d + f + h) * 2.0 + (a + c + g + i);
    FragColor = vec4(max(upsample * (1.0 / 16.0), vec3(0.0)), 1.0);
}
