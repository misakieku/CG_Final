#include "game/render/passes/PostProcessPass.hpp"

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

constexpr const char *kPostFragSrc = R"glsl(
#version 460 core
in vec2 vUV;

layout(std140, binding = 0) uniform FrameUBO
{
    mat4 uView;
    mat4 uProj;
    mat4 uInvProj;
    mat4 uViewProj;
    mat4 uInvViewProj;
    vec4 uCameraPos;
    vec4 uFrustumPlanes[6];
    vec4 uScreenParams;
    vec4 uTimeParams;
    uvec4 uCounts;
    uvec4 uClusterDims;
};

layout(binding = 0) uniform sampler2D uHdrTex;
layout(binding = 1) uniform sampler2D uDepthTex; // Reversed-Z [0, 1]

out vec4 FragColor;

vec3 ACESFilm(vec3 x)
{
    float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 hdr = texture(uHdrTex, vUV).rgb;
    float depth = texture(uDepthTex, vUV).r;
    vec3 skyColor = vec3(0.012, 0.018, 0.036);

    vec3 color = hdr;
    if (depth > 0.0)
    {
        vec4 ndc = vec4(vUV * 2.0 - 1.0, depth, 1.0);
        vec4 worldH = uInvViewProj * ndc;
        vec3 worldPos = worldH.xyz / worldH.w;
        float dist = length(uCameraPos.xyz - worldPos);
        float fogFactor = 1.0 - exp(-dist * 0.038);
        color = mix(hdr, skyColor, clamp(fogFactor, 0.0, 1.0));
    }

    vec3 mapped = ACESFilm(color);
    FragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}
)glsl";

} // namespace

PostProcessPass::PostProcessPass()
    : m_postShader(gl::Shader::compile(kFullscreenVertSrc, kPostFragSrc))
{
}

void PostProcessPass::execute(const RenderContext &ctx, RenderResources &res)
{
    gl::Framebuffer::bindDefault();
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    m_postShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    glBindTextureUnit(0, res.hdrTex().id());
    glBindTextureUnit(1, res.depthTex().id());

    res.drawFullscreenTriangle();
}

} // namespace game::render
