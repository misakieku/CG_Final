#include "game/render/passes/CompositePass.hpp"

#include <glad/gl.h>

namespace game::render
{

namespace
{

constexpr const char *kFullscreenVertSrc = R"glsl(
#version 460 core
out vec2 vUV;

void main()
{
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

constexpr const char *kCompositeFragSrc = R"glsl(
#version 460 core
in vec2 vUV;

layout(std140, binding = 0) uniform FrameUBO
{
    mat4 uView;
    mat4 uProj;
    mat4 uViewProj;
    mat4 uInvViewProj;
    vec4 uCameraPos;
    vec4 uFrustumPlanes[6];
    vec4 uScreenParams;
    vec4 uTimeParams;
    uvec4 uCounts;
};

layout(binding = 0) uniform sampler2D uGBuffer0; // Albedo.rgb + AO
layout(binding = 1) uniform sampler2D uGBuffer1; // Normal.xyz + Roughness
layout(binding = 2) uniform sampler2D uDepthTex; // Reversed-Z [0, 1]

out vec4 FragColor;

vec3 ACESFilm(vec3 x)
{
    float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    float depth = texture(uDepthTex, vUV).r;
    vec3 skyColor = vec3(0.015, 0.022, 0.042);
    if (depth <= 0.0)
    {
        vec3 mapped = ACESFilm(skyColor);
        FragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
        return;
    }

    vec4 g0 = texture(uGBuffer0, vUV);
    vec4 g1 = texture(uGBuffer1, vUV);

    vec3 albedo = g0.rgb;
    float ao = g0.a;
    vec3 N = normalize(g1.rgb);
    float roughness = clamp(g1.a, 0.04, 1.0);

    vec4 ndc = vec4(vUV * 2.0 - 1.0, depth, 1.0);
    vec4 worldH = uInvViewProj * ndc;
    vec3 worldPos = worldH.xyz / worldH.w;
    vec3 V = normalize(uCameraPos.xyz - worldPos);

    // Dim cool moonlight starter shading (replace/extend with FPTL + Shadow + Fog passes)
    vec3 L = normalize(vec3(0.45, 0.85, 0.35));
    vec3 H = normalize(L + V);
    float NdotL = max(dot(N, L), 0.0);
    float shininess = mix(128.0, 8.0, roughness * roughness);
    float spec = pow(max(dot(N, H), 0.0), shininess) * (1.0 - roughness) * 0.6;

    vec3 moonColor = vec3(0.45, 0.58, 0.90);
    vec3 ambient = albedo * vec3(0.11, 0.13, 0.18) * ao;
    vec3 direct = (albedo * NdotL + vec3(spec)) * moonColor * 0.85;

    // Preview player lantern so roofed dungeon corridors are clearly readable before FPTL is added
    float dist = length(uCameraPos.xyz - worldPos);
    float atten = 1.0 / (1.0 + 0.14 * dist + 0.045 * dist * dist);
    float lanternNdotL = max(dot(N, V), 0.0);
    vec3 lantern = albedo * lanternNdotL * vec3(1.15, 0.82, 0.48) * atten * 1.4;

    float fogFactor = 1.0 - exp(-dist * 0.045);
    vec3 color = mix(ambient + direct + lantern, skyColor, clamp(fogFactor, 0.0, 1.0));

    color = ACESFilm(color);
    FragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
)glsl";

} // namespace

CompositePass::CompositePass()
    : m_compositeShader(gl::Shader::compile(kFullscreenVertSrc, kCompositeFragSrc))
{
}

void CompositePass::execute(const RenderContext &ctx, RenderResources &res)
{
    gl::Framebuffer::bindDefault();
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    m_compositeShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    glBindTextureUnit(0, res.gbuffer0().id());
    glBindTextureUnit(1, res.gbuffer1().id());
    glBindTextureUnit(2, res.depthTex().id());

    res.drawFullscreenTriangle();
}

} // namespace game::render
