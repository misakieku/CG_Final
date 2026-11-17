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
    std::vector<glm::vec3> treasureBasePositions;
    std::vector<std::uint32_t> treasureLightIndices;
    float treasureScale = 0.42f;
    std::uint32_t exitObjectId = 0;
    std::uint32_t exitLightIndex = 0;
    std::vector<render::GpuPointLight> pointLights;
};

/// Populates MegaScene from MazeMapData and collects point lights (torches, candles, treasure/exit beacons).
[[nodiscard]] MazeSceneHandles buildMazeScene(const MazeMapData &map, render::MegaScene &scene, const std::filesystem::path &exeDir, float wallHeight = 3.5f);

} // namespace game
