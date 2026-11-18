#pragma once

#include "game/MazeGenerator.hpp"
#include "game/render/GrassField.hpp"
#include "game/render/MegaScene.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace game
{

/// Oriented 2D ground footprint used to prevent grass and bushes from clipping solid slabs, planks, or props.
struct FloorObstacleObb
{
    glm::vec2 center{0.0f};
    glm::vec2 axisX{1.0f, 0.0f};
    glm::vec2 axisZ{0.0f, 1.0f};
    glm::vec2 halfExtents{0.5f};
    bool isSurfaceProp = true;

    [[nodiscard]] static FloorObstacleObb fromYaw(glm::vec2 worldXZ, float yawRad, glm::vec2 halfExt, bool surfaceProp = true) noexcept;
};

struct FoliageGenDesc
{
    bool enableBushes = true;
    std::uint32_t bushSeed = 918273u;
    float bushEdgeChance = 0.68f;
    float bushInteriorChance = 0.24f;

    bool enableGrass = true;
    std::uint32_t grassSeed = 553129u;
    int grassGridCourtyard = 10;
    int grassGridCorridor = 8;
    float grassMinObstacleClearance = 0.105f;
};

/// Populates stylized botanical bushes in open courtyards, records footprints, and returns placed world centers.
[[nodiscard]] std::vector<glm::vec3> populateCourtyardBushes(const MazeMapData &map, render::MegaScene &scene, std::uint32_t bushMaterialId, std::vector<FloorObstacleObb> &obstacles, const FoliageGenDesc &desc = {});

/// Generates OBB-filtered, puddle-avoiding GPU grass tufts across walkable dungeon cells.
[[nodiscard]] std::vector<render::GrassInstance> generateMazeGrass(const MazeMapData &map, const std::vector<FloorObstacleObb> &obstacles, const FoliageGenDesc &desc = {});

} // namespace game
