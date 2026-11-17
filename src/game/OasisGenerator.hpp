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

/// Populates procedural ~2x2-cell desert oases (terraced pool, water, leaning palm tree with coconuts, rocks, and foliage).
void buildMazeOases(const MazeMapData &map, render::MegaScene &scene, std::span<const std::uint32_t> rockMeshes, std::uint32_t emberMeshId, const OasisMaterialIds &materials, std::vector<render::GpuPointLight> &pointLights);

} // namespace game
