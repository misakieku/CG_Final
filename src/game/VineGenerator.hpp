#pragma once

#include "game/MazeGenerator.hpp"
#include "game/render/MegaScene.hpp"

#include <cstdint>

namespace game
{

struct VineGenDesc
{
    bool enabled = true;
    float wallHeight = 3.5f;
    std::uint32_t seed = 1337u;

    // Spawn chances
    float pillarChance = 0.62f;
    float courtyardFloorChance = 0.80f;
    float corridorFloorChance = 0.36f;
    float courtyardRimChance = 0.75f;
    float beamChance = 0.0f; // Keep arches clear of hanging corridor obstructions

    // Growth & size
    int climbStepsMin = 30;
    int climbStepsMax = 52;
    int hangStepsMin = 11;
    int hangStepsMax = 21;
    float stemRadiusScale = 1.0f;
    float leafSizeScale = 1.0f;
    float lateralBranchChance = 0.38f;
    float doubleLeafChance = 0.78f;
};

/// Grows AABB-guided wall-crawling and overhang-hanging vines into chunk-batched MegaScene meshes.
void generateDungeonVines(const MazeMapData &map, render::MegaScene &scene, std::uint32_t materialId, const VineGenDesc &desc = {});

} // namespace game
