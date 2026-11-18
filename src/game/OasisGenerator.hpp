#pragma once

#include "game/MazeGenerator.hpp"
#include "game/render/MegaScene.hpp"
#include "game/render/RenderResources.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace game
{

struct OasisMaterialIds
{
    std::uint32_t stoneWall = 1;
    std::uint32_t warmEmber = 2;
    std::uint32_t sandBank = 4;
    std::uint32_t waterSurface = 5;
    std::uint32_t palmBark = 6;
    std::uint32_t tropicalFoliage = 7;
};

struct OasisGenDesc
{
    bool enabled = true;
    int shoreRockCount = 14;
    float shoreRockScaleMin = 0.22f;
    float shoreRockScaleMax = 0.52f;
    int outerFronds = 7;
    int innerFronds = 3;
    int reedClumps = 3;
    int reedsPerClump = 8;
    bool enableEmberLight = true;
    PointLightDesc emberLight{.color = {1.0f, 0.64f, 0.28f}, .radius = 7.2f, .intensity = 2.5f};
};

/// Populates procedural ~2x2-cell desert oases (terraced pool, water, leaning palm tree with coconuts, rocks, and foliage).
void buildMazeOases(const MazeMapData &map, render::MegaScene &scene, std::span<const std::uint32_t> rockMeshes, std::uint32_t emberMeshId, const OasisMaterialIds &materials, std::vector<render::GpuPointLight> &pointLights, const OasisGenDesc &desc = {});

} // namespace game
