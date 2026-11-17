#include "game/render/passes/GBufferPass.hpp"

#include <glad/gl.h>

namespace game::render
{

namespace
{

constexpr const char *kGBufferVertSrc = R"glsl(
#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

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

struct DrawObjectData
{
    mat4 model;
    mat4 normalMatrix;
    vec4 worldSphere;
    uint meshId;
    uint materialId;
    uint flags;
    uint pad;
};

layout(std430, binding = 0) readonly buffer ObjectsBuffer
{
    DrawObjectData Objects[];
};

layout(std430, binding = 3) readonly buffer VisibleIndicesBuffer
{
    uint VisibleIndices[];
};

out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec2 vUV;
flat out uint vMaterialId;
flat out uint vFlags;

void main()
{
    uint objId = VisibleIndices[uint(gl_BaseInstance) + uint(gl_InstanceID)];
    DrawObjectData obj = Objects[objId];

    vec4 worldPos = obj.model * vec4(aPos, 1.0);
    if ((obj.flags & 4u) != 0u)
    {
        float swayWeight = clamp(aUV.y, 0.0, 1.0);
        float t = uTimeParams.x * 1.65;
        float phase = worldPos.x * 1.3 + worldPos.z * 1.7 + worldPos.y * 0.9;
        worldPos.x += sin(t + phase) * 0.045 * swayWeight;
        worldPos.z += cos(t * 0.85 + phase * 1.2) * 0.045 * swayWeight;
    }

    vWorldPos = worldPos.xyz;
    vWorldNormal = normalize(mat3(obj.normalMatrix) * aNormal);
    vUV = aUV;
    vMaterialId = obj.materialId;
    vFlags = obj.flags;

    gl_Position = uViewProj * worldPos;
}
)glsl";

constexpr const char *kGBufferFragSrc = R"glsl(
#version 460 core
in vec3 vWorldPos;
in vec3 vWorldNormal;
in vec2 vUV;
flat in uint vMaterialId;
flat in uint vFlags;

layout(binding = 0) uniform sampler2DArray uMaterials;

layout(location = 0) out vec4 outGBuffer0; // Albedo.rgb + AO
layout(location = 1) out vec4 outGBuffer1; // WorldNormal.xyz + Roughness

vec2 computeWorldBoxUV(vec3 worldPos, vec3 worldNormal)
{
    vec3 an = abs(worldNormal);
    vec2 uv;
    if (an.x >= an.y && an.x >= an.z)
    {
        uv = worldPos.zy;
    }
    else if (an.y >= an.x && an.y >= an.z)
    {
        uv = worldPos.xz;
    }
    else
    {
        uv = worldPos.xy;
    }
    return uv * 0.333333; // 1 texture tile per 3.0m maze cell
}

void main()
{
    vec3 N = normalize(vWorldNormal);
    vec2 uv = ((vFlags & 2u) != 0u) ? computeWorldBoxUV(vWorldPos, N) : vUV;
    vec4 matSample = texture(uMaterials, vec3(uv, float(vMaterialId)));

    outGBuffer0 = vec4(matSample.rgb, 1.0);
    outGBuffer1 = vec4(N, matSample.a);
}
)glsl";

} // namespace

GBufferPass::GBufferPass()
    : m_gbufferShader(gl::Shader::compile(kGBufferVertSrc, kGBufferFragSrc))
{
}

void GBufferPass::execute(const RenderContext &ctx, RenderResources &res)
{
    res.gbufferFbo().bind();
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);

    // Reversed-Z depth state: clear to 0.0 (far), pass fragments with GREATER depth
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_GREATER);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepth(0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const MegaScene &scene = res.scene();
    if (scene.meshCount() == 0 || scene.objectCount() == 0)
    {
        return;
    }

    m_gbufferShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    scene.bindSceneBuffers(0, 1, 2, 3);
    glBindTextureUnit(0, res.materialArrayTexId());

    scene.bindMegaVao();
    scene.bindDrawIndirectBuffer();
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(scene.meshCount()), 0);

    glBindVertexArray(0);
}

} // namespace game::render
