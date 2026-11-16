#pragma once

#include "game/MazeGenerator.hpp"
#include "game/render/MegaScene.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace game
{

struct MazeSceneHandles
{
    std::vector<std::uint32_t> treasureObjectIds;
    std::uint32_t exitObjectId = 0;
};

/// Populates MegaScene from MazeMapData (walls, floor, roofed corridors/open courtyards, stepped pillars, beams, torches, candles, rubble, and SOR treasures).
[[nodiscard]] MazeSceneHandles buildMazeScene(const MazeMapData &map, render::MegaScene &scene, const std::filesystem::path &exeDir, float wallHeight = 3.5f);

} // namespace game
