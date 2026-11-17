#pragma once

#include "common/lighting_common.glsl"

layout(std430, binding = 4) readonly buffer PointLightsBuffer
{
    GpuPointLight Lights[];
};

layout(std430, binding = 5) readonly buffer ClusterCountsBuffer
{
    uint ClusterCounts[];
};

layout(std430, binding = 6) readonly buffer ClusterIndicesBuffer
{
    uint ClusterLightIndices[];
};
