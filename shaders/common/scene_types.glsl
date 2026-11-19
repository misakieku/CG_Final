#pragma once

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

struct MeshSpan
{
    vec4 boundingSphere;
    vec4 aabbMin;
    vec4 aabbMax;
    uint indexCount;
    uint firstIndex;
    int  baseVertex;
    uint baseInstanceOffset;
};

struct DrawElementsIndirectCommand
{
    uint count;
    uint instanceCount;
    uint firstIndex;
    int  baseVertex;
    uint baseInstance;
};

const uint kObjectVisibleFlag = 1u;
const uint kObjectWorldUVFlag = 2u;
const uint kObjectWindSwayFlag = 4u;
const uint kObjectWaterFlag = 8u;
const uint kObjectStoneDetailFlag = 16u;
const uint kObjectNoPointShadowFlag = 32u;
