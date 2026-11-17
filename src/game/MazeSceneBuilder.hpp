#pragma once

#include "game/MazeGenerator.hpp"
#include "game/render/MegaScene.hpp"
#include "game/render/RenderResources.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace game
{

struct MazeSceneHandles
{
    std::vector<std::uint32_t> treasureObjectIds;
    std::uint32_t exitObjectId = 0;
    std::vector<render::GpuPointLight> pointLights;
};

/// Populates MegaScene from MazeMapData and collects point lights (torches, candles, treasure/exit beacons).
[[nodiscard]] MazeSceneHandles buildMazeScene(const MazeMapData &map, render::MegaScene &scene, const std::filesystem::path &exeDir, float wallHeight = 3.5f);

} // namespace game
