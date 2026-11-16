#include "game/render/passes/CullingPass.hpp"

#include <glad/gl.h>

namespace game::render
{

namespace
{

constexpr const char *kCullComputeSrc = R"glsl(
#version 460 core
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

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
    uvec4 uCounts; // x = objectCount, y = meshCount
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

struct DrawElementsIndirectCommand
{
    uint count;
    uint instanceCount;
    uint firstIndex;
    int  baseVertex;
    uint baseInstance;
};

layout(std430, binding = 0) readonly buffer ObjectsBuffer
{
    DrawObjectData Objects[];
};

layout(std430, binding = 2) buffer IndirectCmdBuffer
{
    DrawElementsIndirectCommand Cmds[];
};

layout(std430, binding = 3) writeonly buffer VisibleIndicesBuffer
{
    uint VisibleIndices[];
};

bool isSphereInFrustum(vec4 sphere)
{
    for (int i = 0; i < 6; ++i)
    {
        if (dot(uFrustumPlanes[i].xyz, sphere.xyz) + uFrustumPlanes[i].w < -sphere.w)
        {
            return false;
        }
    }
    return true;
}

void main()
{
    uint objId = gl_GlobalInvocationID.x;
    if (objId >= uCounts.x)
    {
        return;
    }

    DrawObjectData obj = Objects[objId];
    if ((obj.flags & 1u) == 0u || !isSphereInFrustum(obj.worldSphere))
    {
        return;
    }

    uint localSlot = atomicAdd(Cmds[obj.meshId].instanceCount, 1u);
    uint globalSlot = Cmds[obj.meshId].baseInstance + localSlot;
    VisibleIndices[globalSlot] = objId;
}
)glsl";

} // namespace

CullingPass::CullingPass()
    : m_cullShader(gl::Shader::compileCompute(kCullComputeSrc))
{
}

void CullingPass::execute(const RenderContext &ctx, RenderResources &res)
{
    (void)ctx;
    const MegaScene &scene = res.scene();
    if (scene.objectCount() == 0 || scene.meshCount() == 0)
    {
        return;
    }

    scene.resetIndirectCommands();
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

    m_cullShader.use();
    res.frameUbo().bindBase(GL_UNIFORM_BUFFER, 0);
    scene.bindSceneBuffers(0, 1, 2, 3);

    const GLuint groupsX = (scene.objectCount() + 63u) / 64u;
    glDispatchCompute(groupsX, 1, 1);
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
}

} // namespace game::render
