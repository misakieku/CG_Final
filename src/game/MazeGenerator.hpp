#pragma once

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace game
{

enum class CellKind : uint8_t
{
    Wall,
    Corridor,
    Room,
    Courtyard,
    Exit
};

struct PropSpawn
{
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float yaw = 0.0f;
    float scale = 1.0f;
};

struct OasisSpawn
{
    glm::vec3 center{0.0f};
    float radiusX = 2.25f;
    float radiusZ = 1.70f;
    float yaw = 0.0f;
    std::uint32_t seed = 0;
};

struct PointLightDesc
{
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float radius = 6.0f;
    float intensity = 2.0f;
};

struct MazeMapData
{
    glm::ivec2 size{0};
    float cellSize = 3.0f;
    std::vector<CellKind> cells;

    glm::ivec2 startCell{1, 1};
    glm::ivec2 exitCell{1, 1};
    glm::vec3 playerStart{0.0f};
    glm::vec3 exitPos{0.0f};

    std::vector<glm::ivec2> deadEnd;
    std::vector<glm::vec3> treasurePositions;
    std::vector<glm::vec3> pillars;
    std::vector<PropSpawn> ceilingBeams;
    std::vector<PropSpawn> wallTorches;
    std::vector<PropSpawn> candles;
    std::vector<PropSpawn> rubbles;
    std::vector<OasisSpawn> oases;

    [[nodiscard]] bool inBounds(glm::ivec2 point) const noexcept
    {
        return point.x >= 0 && point.y >= 0 && point.x < size.x && point.y < size.y;
    }

    [[nodiscard]] int getCellIndex(int x, int y) const noexcept
    {
        return y * size.x + x;
    }

    [[nodiscard]] CellKind getCell(glm::ivec2 point) const noexcept
    {
        return cells[static_cast<std::size_t>(getCellIndex(point.x, point.y))];
    }

    void setCell(glm::ivec2 point, CellKind cell) noexcept
    {
        cells[static_cast<std::size_t>(getCellIndex(point.x, point.y))] = cell;
    }

    [[nodiscard]] bool isWalkable(glm::ivec2 point) const noexcept
    {
        return inBounds(point) && getCell(point) != CellKind::Wall;
    }

    [[nodiscard]] glm::vec3 cellCenterWorld(glm::ivec2 point, float y = 0.0f) const noexcept
    {
        return glm::vec3((static_cast<float>(point.x) + 0.5f) * cellSize, y, (static_cast<float>(point.y) + 0.5f) * cellSize);
    }

    [[nodiscard]] glm::ivec2 worldToCell(glm::vec3 worldPos) const noexcept
    {
        return glm::ivec2(static_cast<int>(std::floor(worldPos.x / cellSize)), static_cast<int>(std::floor(worldPos.z / cellSize)));
    }
};

struct MazeGenDesc
{
    glm::ivec2 size{21, 21};
    float cellSize = 3.0f;
    glm::ivec2 start{1, 1};
    std::uint32_t seed = 0; // 0 uses std::random_device

    // Courtyard & loop topology (< 0 auto-scales with map size)
    int minCourtyards = -1;
    int maxCourtyards = -1;
    float braidRatio = 0.38f;
    float corridorNewestBias = 0.72f;
    float corridorStraightBias = 0.58f;
    int maxUnbranchedSteps = 3;
    int extraLoopCount = 2;

    // Treasures
    int maxTreasures = 5;
    int minTreasureSeparation = 4;
    int minTreasureStartSteps = 4;

    // Overhead stone ceiling beams / corridor arches
    bool enableBeams = true;
    int minBeamRunCells = 2;
    float beamEndMargin = 1.65f;
    float beamSpacing = 9.20f;

    // Courtyard desert oasis footprints
    bool enableOases = false;
    int minOasisCourtyardSize = 5;
    float oasisRadiusX = 2.15f;
    float oasisRadiusZ = 1.60f;

    // Wall torches
    bool enableTorches = true;
    float torchMinSpacingCells = 1.45f;
    float torchWallChance = 0.75f;
    float torchMountHeight = 2.1f;

    // Candle cluster anchors along walls
    bool enableCandles = true;
    float candleWallChance = 0.3f;
    int shrineCandleClusters = 2;
    float candleScaleMin = 0.85f;
    float candleScaleMax = 1.15f;

    // Floor rubble rocks
    bool enableRubble = true;
    float rubbleExtraWallChance = 0.55f;
    int courtyardExtraRocks = 2;
    float rubbleScaleMin = 0.10f;
    float rubbleScaleMax = 0.26f;
};

/// Generates a connected odd-grid dungeon maze with open-sky courtyards, dead-end treasures, pillars, torches, candles, and rubble.
MazeMapData generateMaze(const MazeGenDesc &desc = {});

} // namespace game
