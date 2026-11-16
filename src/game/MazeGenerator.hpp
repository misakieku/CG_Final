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

/// Generates a connected odd-grid dungeon maze with open-sky courtyards, dead-end treasures, pillars, torches, candles, and rubble.
MazeMapData generateMaze(glm::ivec2 size, float cellSize, glm::ivec2 start, std::uint32_t seed = 0);

} // namespace game
