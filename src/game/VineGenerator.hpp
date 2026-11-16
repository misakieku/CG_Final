#pragma once

#include "game/MazeGenerator.hpp"
#include "game/render/MegaScene.hpp"

#include <cstdint>

namespace game
{

/// Grows AABB-guided wall-crawling and overhang-hanging vines into chunk-batched MegaScene meshes.
void generateDungeonVines(const MazeMapData &map, render::MegaScene &scene, std::uint32_t materialId, float wallHeight = 3.5f, std::uint32_t seed = 1337u);

} // namespace game
