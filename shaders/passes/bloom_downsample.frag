#version 460 core
in vec2 vUV;

layout(binding = 0) uniform sampler2D uBloomTex;

uniform vec2 uSrcTexelSize;

out vec4 FragColor;

void main()
{
    float x = uSrcTexelSize.x;
    float y = uSrcTexelSize.y;

    vec3 a = textureLod(uBloomTex, vec2(vUV.x - 2.0 * x, vUV.y + 2.0 * y), 0.0).rgb;
    vec3 b = textureLod(uBloomTex, vec2(vUV.x,           vUV.y + 2.0 * y), 0.0).rgb;
    vec3 c = textureLod(uBloomTex, vec2(vUV.x + 2.0 * x, vUV.y + 2.0 * y), 0.0).rgb;

    vec3 d = textureLod(uBloomTex, vec2(vUV.x - 2.0 * x, vUV.y),           0.0).rgb;
    vec3 e = textureLod(uBloomTex, vec2(vUV.x,           vUV.y),           0.0).rgb;
    vec3 f = textureLod(uBloomTex, vec2(vUV.x + 2.0 * x, vUV.y),           0.0).rgb;

    vec3 g = textureLod(uBloomTex, vec2(vUV.x - 2.0 * x, vUV.y - 2.0 * y), 0.0).rgb;
    vec3 h = textureLod(uBloomTex, vec2(vUV.x,           vUV.y - 2.0 * y), 0.0).rgb;
    vec3 i = textureLod(uBloomTex, vec2(vUV.x + 2.0 * x, vUV.y - 2.0 * y), 0.0).rgb;

    vec3 j = textureLod(uBloomTex, vec2(vUV.x - x,       vUV.y + y),       0.0).rgb;
    vec3 k = textureLod(uBloomTex, vec2(vUV.x + x,       vUV.y + y),       0.0).rgb;
    vec3 l = textureLod(uBloomTex, vec2(vUV.x - x,       vUV.y - y),       0.0).rgb;
    vec3 m = textureLod(uBloomTex, vec2(vUV.x + x,       vUV.y - y),       0.0).rgb;

    vec3 result = (j + k + l + m) * 0.125
                + e * 0.125
                + (b + d + f + h) * 0.0625
                + (a + c + g + i) * 0.03125;

    FragColor = vec4(max(result, vec3(0.0)), 1.0);
}
