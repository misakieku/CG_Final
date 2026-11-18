#pragma once

#include "game/MazeGenerator.hpp"
#include "game/OasisGenerator.hpp"
#include "game/VineGenerator.hpp"
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

struct MazeSceneDesc
{
    float wallHeight = 3.5f;
    bool buildCourtyardParapets = true;
    bool buildTempleTrim = true;
    bool buildCorridorArches = true;

    // Upper floor chipped flagstones & weathered timber planks
    bool buildFloorPavers = true;
    int paversPerCellMin = 4;
    int paversPerCellMax = 6;
    float paverMinSeparation = 0.56f;
    float paverScaleMin = 0.84f;
    float paverScaleMax = 1.18f;
    float paverMaxYawJitterDeg = 15.0f;

    bool buildFloorPlanks = true;
    float plankCellChance = 0.42f;
    int planksPerClusterMax = 2;
    float plankLengthMin = 1.05f;
    float plankLengthMax = 1.65f;
    float plankWidth = 0.21f;
    float plankThickness = 0.038f;
    float plankTiltMaxDeg = 5.5f;

    std::uint32_t propSeed = 424242u;

    // Wall torches
    float torchScale = 1.0f;
    PointLightDesc torchLight{.color = {1.0f, 0.52f, 0.18f}, .radius = 9.2f, .intensity = 2.5f};

    // Clustered floor candles
    int candleClusterMinCount = 2;
    int candleClusterMaxCount = 4;
    float candleClusterRadius = 0.22f;
    float candleHeight = 0.28f;
    float candleScale = 1.0f;
    PointLightDesc candleLight{.color = {1.0f, 0.62f, 0.24f}, .radius = 3.3f, .intensity = 0.78f};

    // Treasures, pottery accents & exit beacon
    float treasureHeight = 0.85f;
    PointLightDesc treasureLight{.color = {1.0f, 0.80f, 0.35f}, .radius = 4.2f, .intensity = 1.25f};
    PointLightDesc exitLight{.color = {0.25f, 0.95f, 0.65f}, .radius = 7.5f, .intensity = 3.8f};

    bool enableVasesAndCups = true;
    float vaseCourtyardChance = 0.28f;
    float vaseCorridorChance = 0.09f;
    float cupWithVaseChance = 0.45f;
    float vaseHeight = 0.74f;
    float cupHeight = 0.28f;

    OasisGenDesc oasis{.enabled = false};
    VineGenDesc vines{};
};

/// Populates MegaScene from MazeMapData and collects point lights (torches, candles, treasure/exit beacons).
[[nodiscard]] MazeSceneHandles buildMazeScene(const MazeMapData &map, render::MegaScene &scene, const std::filesystem::path &exeDir, const MazeSceneDesc &desc = {});

} // namespace game
