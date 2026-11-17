#include "game/MazeGenerator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <random>
#include <stack>

namespace game
{

namespace
{

struct Courtyard
{
    glm::ivec2 start{0};
    glm::ivec2 size{0};
};

constexpr std::array<glm::ivec2, 4> kCardinalDirs = {
    glm::ivec2(-1, 0),
    glm::ivec2(1, 0),
    glm::ivec2(0, -1),
    glm::ivec2(0, 1),
};

[[nodiscard]] bool overlapsWithPadding(const Courtyard &a, const Courtyard &b, int padding) noexcept
{
    return a.start.x < b.start.x + b.size.x + padding && a.start.x + a.size.x + padding > b.start.x &&
           a.start.y < b.start.y + b.size.y + padding && a.start.y + a.size.y + padding > b.start.y;
}

[[nodiscard]] bool containsCell(const Courtyard &c, glm::ivec2 p, int padding) noexcept
{
    return p.x >= c.start.x - padding && p.x < c.start.x + c.size.x + padding && p.y >= c.start.y - padding &&
           p.y < c.start.y + c.size.y + padding;
}

void fillCourtyard(MazeMapData &data, glm::ivec2 start, std::mt19937 &eng, std::vector<Courtyard> &courtyards)
{
    // Leave at least a 2-cell corridor ring around the outer border so DFS stays globally connected
    const int maxAvailW = data.size.x - 6;
    const int maxAvailH = data.size.y - 6;
    if (maxAvailW < 3 || maxAvailH < 3)
    {
        return;
    }

    const int maxOddK = std::min(3, (std::min(maxAvailW, maxAvailH) - 1) / 2);
    std::uniform_int_distribution<> sizeDist(1, std::max(1, maxOddK));

    for (int attempt = 0; attempt < 24; ++attempt)
    {
        const int width = std::min(maxAvailW, sizeDist(eng) * 2 + 1);
        const int height = std::min(maxAvailH, sizeDist(eng) * 2 + 1);

        const int maxIdxX = (data.size.x - 3 - width) / 2;
        const int maxIdxY = (data.size.y - 3 - height) / 2;
        if (maxIdxX < 1 || maxIdxY < 1)
        {
            continue;
        }

        const int x = std::uniform_int_distribution<>(1, maxIdxX)(eng) * 2 + 1;
        const int y = std::uniform_int_distribution<>(1, maxIdxY)(eng) * 2 + 1;
        const Courtyard candidate{{x, y}, {width, height}};

        if (containsCell(candidate, start, 1))
        {
            continue;
        }
        bool overlaps = false;
        for (const Courtyard &existing : courtyards)
        {
            if (overlapsWithPadding(candidate, existing, 2))
            {
                overlaps = true;
                break;
            }
        }
        if (overlaps)
        {
            continue;
        }

        for (int i = y; i < y + height; ++i)
        {
            for (int j = x; j < x + width; ++j)
            {
                data.setCell({j, i}, CellKind::Courtyard);
            }
        }
        courtyards.push_back(candidate);
        return;
    }
}

void fillCorridor(MazeMapData &data, glm::ivec2 start, std::mt19937 &eng)
{
    data.setCell(start, CellKind::Corridor);

    std::stack<glm::ivec2> paths;
    paths.push(start);

    std::array<glm::ivec2, 4> dirs = {
        glm::ivec2(-2, 0),
        glm::ivec2(0, 2),
        glm::ivec2(2, 0),
        glm::ivec2(0, -2),
    };

    while (!paths.empty())
    {
        const glm::ivec2 point = paths.top();
        std::shuffle(dirs.begin(), dirs.end(), eng);

        bool carved = false;
        for (const glm::ivec2 dir : dirs)
        {
            const glm::ivec2 target = point + dir;
            if (target.x > 0 && target.y > 0 && target.x < data.size.x - 1 && target.y < data.size.y - 1 &&
                data.getCell(target) == CellKind::Wall)
            {
                data.setCell(point + dir / 2, CellKind::Corridor);
                data.setCell(target, CellKind::Corridor);
                paths.push(target);
                carved = true;
                break;
            }
        }

        if (!carved)
        {
            paths.pop();
        }
    }
}

void openCourtyard(MazeMapData &data, std::mt19937 &eng, const std::vector<Courtyard> &courtyards)
{
    for (const Courtyard &courtyard : courtyards)
    {
        const int doorCountX = std::max(1, courtyard.size.x >> 2);
        const int doorCountY = std::max(1, courtyard.size.y >> 2);

        auto openEdge = [&](glm::ivec2 firstWall, glm::ivec2 step, int oddCount, glm::ivec2 outward, int doorQuota) {
            std::vector<glm::ivec2> candidates;
            for (int i = 0; i < oddCount; ++i)
            {
                const glm::ivec2 wall = firstWall + step * (i * 2);
                const glm::ivec2 outside = wall + outward;
                if (outside.x > 0 && outside.y > 0 && outside.x < data.size.x - 1 && outside.y < data.size.y - 1 &&
                    data.getCell(wall) == CellKind::Wall && data.getCell(outside) == CellKind::Corridor)
                {
                    candidates.push_back(wall);
                }
            }
            std::shuffle(candidates.begin(), candidates.end(), eng);
            const int toOpen = std::min(doorQuota, static_cast<int>(candidates.size()));
            for (int k = 0; k < toOpen; ++k)
            {
                data.setCell(candidates[static_cast<std::size_t>(k)], CellKind::Corridor);
            }
        };

        const int oddX = (courtyard.size.x + 1) / 2;
        const int oddY = (courtyard.size.y + 1) / 2;

        openEdge({courtyard.start.x, courtyard.start.y - 1}, {1, 0}, oddX, {0, -1}, doorCountX);
        openEdge({courtyard.start.x, courtyard.start.y + courtyard.size.y}, {1, 0}, oddX, {0, 1}, doorCountX);
        openEdge({courtyard.start.x - 1, courtyard.start.y}, {0, 1}, oddY, {-1, 0}, doorCountY);
        openEdge({courtyard.start.x + courtyard.size.x, courtyard.start.y}, {0, 1}, oddY, {1, 0}, doorCountY);
    }
}

[[nodiscard]] std::vector<int> computeBfsDistances(const MazeMapData &data, glm::ivec2 start)
{
    std::vector<int> dist(data.cells.size(), -1);
    std::queue<glm::ivec2> q;
    dist[static_cast<std::size_t>(data.getCellIndex(start.x, start.y))] = 0;
    q.push(start);

    while (!q.empty())
    {
        const glm::ivec2 cur = q.front();
        q.pop();
        const int curDist = dist[static_cast<std::size_t>(data.getCellIndex(cur.x, cur.y))];

        for (const glm::ivec2 d : kCardinalDirs)
        {
            const glm::ivec2 next = cur + d;
            if (!data.isWalkable(next))
            {
                continue;
            }
            const auto nextIdx = static_cast<std::size_t>(data.getCellIndex(next.x, next.y));
            if (dist[nextIdx] == -1)
            {
                dist[nextIdx] = curDist + 1;
                q.push(next);
            }
        }
    }
    return dist;
}

void resolveDeadEndsExitAndTreasures(MazeMapData &data, glm::ivec2 start, std::mt19937 &eng)
{
    const std::vector<int> dist = computeBfsDistances(data, start);

    int maxDist = 0;
    glm::ivec2 farthestWalkable = start;
    for (int y = 1; y < data.size.y - 1; ++y)
    {
        for (int x = 1; x < data.size.x - 1; ++x)
        {
            const glm::ivec2 cell{x, y};
            const int d = dist[static_cast<std::size_t>(data.getCellIndex(x, y))];
            if (d < 0)
            {
                continue;
            }
            if (d > maxDist)
            {
                maxDist = d;
                farthestWalkable = cell;
            }

            if (cell == start || data.getCell(cell) != CellKind::Corridor)
            {
                continue;
            }

            int walkableNeighbors = 0;
            for (const glm::ivec2 dir : kCardinalDirs)
            {
                if (data.isWalkable(cell + dir))
                {
                    ++walkableNeighbors;
                }
            }
            if (walkableNeighbors == 1)
            {
                data.deadEnd.push_back(cell);
            }
        }
    }

    // Place Exit at the deepest dead end (fallback to farthest reachable cell)
    glm::ivec2 exitCell = farthestWalkable;
    int bestExitDist = -1;
    for (const glm::ivec2 cell : data.deadEnd)
    {
        const int d = dist[static_cast<std::size_t>(data.getCellIndex(cell.x, cell.y))];
        if (d > bestExitDist)
        {
            bestExitDist = d;
            exitCell = cell;
        }
    }

    data.exitCell = exitCell;
    data.setCell(exitCell, CellKind::Exit);
    data.exitPos = data.cellCenterWorld(exitCell);

    // Pick well-spaced dead ends (excluding Exit and cells too close to start) for SOR treasures
    std::vector<glm::ivec2> treasureCandidates;
    const int minTreasureSteps = std::max(4, maxDist / 4);
    for (const glm::ivec2 cell : data.deadEnd)
    {
        if (cell == exitCell)
        {
            continue;
        }
        const int d = dist[static_cast<std::size_t>(data.getCellIndex(cell.x, cell.y))];
        if (d >= minTreasureSteps)
        {
            treasureCandidates.push_back(cell);
        }
    }
    if (treasureCandidates.empty())
    {
        for (const glm::ivec2 cell : data.deadEnd)
        {
            if (cell != exitCell)
            {
                treasureCandidates.push_back(cell);
            }
        }
    }

    std::shuffle(treasureCandidates.begin(), treasureCandidates.end(), eng);
    constexpr std::size_t kMaxTreasures = 5;
    std::vector<glm::ivec2> chosenCells;
    for (const glm::ivec2 cell : treasureCandidates)
    {
        if (chosenCells.size() >= kMaxTreasures)
        {
            break;
        }
        bool tooClose = false;
        for (const glm::ivec2 picked : chosenCells)
        {
            const int manhattan = std::abs(cell.x - picked.x) + std::abs(cell.y - picked.y);
            if (manhattan < 4)
            {
                tooClose = true;
                break;
            }
        }
        if (!tooClose)
        {
            chosenCells.push_back(cell);
        }
    }

    // Top up if spacing filter left fewer than 3 treasures on smaller maps
    for (const glm::ivec2 cell : treasureCandidates)
    {
        if (chosenCells.size() >= std::min<std::size_t>(3, treasureCandidates.size()))
        {
            break;
        }
        if (std::find(chosenCells.begin(), chosenCells.end(), cell) == chosenCells.end())
        {
            chosenCells.push_back(cell);
        }
    }

    data.treasurePositions.reserve(chosenCells.size());
    for (const glm::ivec2 cell : chosenCells)
    {
        data.treasurePositions.push_back(data.cellCenterWorld(cell));
    }
}

void braidSpareDeadEnds(MazeMapData &data, std::mt19937 &eng, float braidRatio = 0.38f)
{
    std::vector<glm::ivec2> protectedCells;
    protectedCells.push_back(data.startCell);
    protectedCells.push_back(data.exitCell);
    for (const glm::vec3 &tp : data.treasurePositions)
    {
        protectedCells.push_back(data.worldToCell(tp));
    }

    auto isProtected = [&](glm::ivec2 cell) noexcept -> bool
    {
        return std::find(protectedCells.begin(), protectedCells.end(), cell) != protectedCells.end();
    };

    std::vector<glm::ivec2> spareDeadEnds;
    for (const glm::ivec2 cell : data.deadEnd)
    {
        if (!isProtected(cell))
        {
            spareDeadEnds.push_back(cell);
        }
    }

    std::shuffle(spareDeadEnds.begin(), spareDeadEnds.end(), eng);
    const std::size_t targetBreaks = static_cast<std::size_t>(std::round(static_cast<float>(spareDeadEnds.size()) * braidRatio));

    std::array<glm::ivec2, 4> dirs = kCardinalDirs;
    std::size_t broken = 0;
    for (const glm::ivec2 cell : spareDeadEnds)
    {
        if (broken >= targetBreaks)
        {
            break;
        }
        std::shuffle(dirs.begin(), dirs.end(), eng);
        for (const glm::ivec2 d : dirs)
        {
            const glm::ivec2 wall = cell + d;
            const glm::ivec2 opposite = cell + d * 2;
            if (opposite.x <= 0 || opposite.y <= 0 || opposite.x >= data.size.x - 1 || opposite.y >= data.size.y - 1)
            {
                continue;
            }
            if (data.getCell(wall) != CellKind::Wall || data.getCell(opposite) != CellKind::Corridor || isProtected(opposite))
            {
                continue;
            }

            // Keep perpendicular neighbors walled so breaking this bridge never creates a 2x2 open blob
            const glm::ivec2 perp(-d.y, d.x);
            if (data.getCell(wall + perp) == CellKind::Wall && data.getCell(wall - perp) == CellKind::Wall)
            {
                data.setCell(wall, CellKind::Corridor);
                ++broken;
                break;
            }
        }
    }

    // Refresh deadEnd list so braided cells are removed
    std::erase_if(data.deadEnd, [&](glm::ivec2 cell)
    {
        int walkableNeighbors = 0;
        for (const glm::ivec2 d : kCardinalDirs)
        {
            if (data.isWalkable(cell + d))
            {
                ++walkableNeighbors;
            }
        }
        return walkableNeighbors != 1;
    });
}

void populatePillarsAndBeams(MazeMapData &data)
{
    // Corner pillars at grid vertices shared by 1 wall (outer corner) or 3 walls (inner corner)
    for (int vy = 1; vy < data.size.y; ++vy)
    {
        for (int vx = 1; vx < data.size.x; ++vx)
        {
            int wallCount = 0;
            for (int dy = -1; dy <= 0; ++dy)
            {
                for (int dx = -1; dx <= 0; ++dx)
                {
                    if (data.getCell({vx + dx, vy + dy}) == CellKind::Wall)
                    {
                        ++wallCount;
                    }
                }
            }
            if (wallCount == 1 || wallCount == 3)
            {
                data.pillars.emplace_back(static_cast<float>(vx) * data.cellSize, 0.0f,
                                          static_cast<float>(vy) * data.cellSize);
            }
        }
    }

    // Overhead corridor ceiling beams spanning straight passages
    for (int y = 1; y < data.size.y - 1; ++y)
    {
        for (int x = 1; x < data.size.x - 1; ++x)
        {
            if (data.getCell({x, y}) != CellKind::Corridor || ((x + y) % 4 != 0))
            {
                continue;
            }
            const bool walledEW =
                data.getCell({x - 1, y}) == CellKind::Wall && data.getCell({x + 1, y}) == CellKind::Wall;
            const bool walledNS =
                data.getCell({x, y - 1}) == CellKind::Wall && data.getCell({x, y + 1}) == CellKind::Wall;
            if (walledEW || walledNS)
            {
                const float yaw = walledNS ? 1.5707963f : 0.0f;
                data.ceilingBeams.push_back(PropSpawn{
                    .position = data.cellCenterWorld({x, y}, 0.0f),
                    .normal = glm::vec3(0.0f, -1.0f, 0.0f),
                    .yaw = yaw,
                    .scale = 1.0f,
                });
            }
        }
    }
}

void populateOases(MazeMapData &data, const std::vector<Courtyard> &courtyards, std::mt19937 &eng)
{
    std::uniform_real_distribution<float> pushDist(0.55f, 0.95f);
    std::uniform_real_distribution<float> yawRand(0.0f, 6.2831853f);

    constexpr float kRadiusX = 2.15f;
    constexpr float kRadiusZ = 1.60f;
    constexpr float kSafeFootprintMargin = 4.70f; // ~3.1m oasis outer bank + 1.6m walkable shore margin

    for (const Courtyard &c : courtyards)
    {
        if (c.size.x < 5 || c.size.y < 5)
        {
            continue;
        }

        const float halfW = static_cast<float>(c.size.x) * data.cellSize * 0.5f;
        const float halfH = static_cast<float>(c.size.y) * data.cellSize * 0.5f;
        const float maxOffX = std::max(0.0f, halfW - kSafeFootprintMargin);
        const float maxOffZ = std::max(0.0f, halfH - kSafeFootprintMargin);

        const float angle = yawRand(eng);
        const float push = pushDist(eng);
        const float centerX = (static_cast<float>(c.start.x) + static_cast<float>(c.size.x) * 0.5f) * data.cellSize + std::cos(angle) * maxOffX * push;
        const float centerZ = (static_cast<float>(c.start.y) + static_cast<float>(c.size.y) * 0.5f) * data.cellSize + std::sin(angle) * maxOffZ * push;
        data.oases.push_back(OasisSpawn{
            .center = glm::vec3(centerX, 0.0f, centerZ),
            .radiusX = kRadiusX,
            .radiusZ = kRadiusZ,
            .yaw = yawRand(eng),
            .seed = eng(),
        });
    }
}

void populateProps(MazeMapData &data, std::mt19937 &eng)
{
    std::uniform_real_distribution<float> unitRand(0.0f, 1.0f);
    std::uniform_real_distribution<float> yawRand(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> rubbleScaleRand(0.10f, 0.26f);

    const float halfCell = data.cellSize * 0.5f;
    const float minTorchDistSq = (data.cellSize * 1.45f) * (data.cellSize * 1.45f);

    auto isTreasureCell = [&](glm::ivec2 cell) -> bool {
        const glm::vec3 center = data.cellCenterWorld(cell);
        for (const glm::vec3 &tp : data.treasurePositions)
        {
            const glm::vec2 d(center.x - tp.x, center.z - tp.z);
            if (glm::dot(d, d) < 0.01f)
            {
                return true;
            }
        }
        return false;
    };

    auto hasNearbyTorch = [&](const glm::vec3 &pos) -> bool {
        for (const PropSpawn &t : data.wallTorches)
        {
            const glm::vec2 d(pos.x - t.position.x, pos.z - t.position.z);
            if (glm::dot(d, d) < minTorchDistSq)
            {
                return true;
            }
        }
        return false;
    };

    for (int y = 1; y < data.size.y - 1; ++y)
    {
        for (int x = 1; x < data.size.x - 1; ++x)
        {
            const glm::ivec2 cell{x, y};
            const CellKind kind = data.getCell(cell);
            if (kind == CellKind::Wall)
            {
                continue;
            }

            const glm::vec3 center = data.cellCenterWorld(cell);
            const bool shrineOrExit = (kind == CellKind::Exit) || isTreasureCell(cell);

            for (const glm::ivec2 dir : kCardinalDirs)
            {
                const glm::ivec2 neighbor = cell + dir;
                if (data.inBounds(neighbor) && data.getCell(neighbor) != CellKind::Wall)
                {
                    continue;
                }

                const glm::vec3 outward(static_cast<float>(dir.x), 0.0f, static_cast<float>(dir.y));
                const glm::vec3 inward = -outward;
                const glm::vec3 tangent(-inward.z, 0.0f, inward.x);
                const glm::vec3 wallFace = center + outward * halfCell;
                const float wallYaw = std::atan2(inward.x, inward.z);

                // Wall torches (spaced every ~1.5 cells, including start cell and shrines)
                if (((x * 3 + y * 5) % 2 == 0) || shrineOrExit || cell == data.startCell)
                {
                    const glm::vec3 torchPos = wallFace + inward * 0.18f + glm::vec3(0.0f, 2.1f, 0.0f);
                    if (!hasNearbyTorch(torchPos))
                    {
                        data.wallTorches.push_back(PropSpawn{torchPos, inward, wallYaw, 1.0f});
                    }
                }

                // Floor candles along walls (guaranteed around treasure shrines & exit)
                const int candleCount = shrineOrExit ? 2 : (unitRand(eng) < 0.28f ? 1 : 0);
                for (int c = 0; c < candleCount; ++c)
                {
                    const float sideOffset = (unitRand(eng) - 0.5f) * (data.cellSize * 0.55f);
                    const float depthOffset = 0.25f + unitRand(eng) * 0.18f;
                    const glm::vec3 candlePos = wallFace + inward * depthOffset + tangent * sideOffset;
                    data.candles.push_back(
                        PropSpawn{candlePos, glm::vec3(0.0f, 1.0f, 0.0f), yawRand(eng), 0.85f + 0.3f * unitRand(eng)});
                }

                // Wall-edge rubble keeping the center path clear
                const int rubbleCount = 1 + (unitRand(eng) < 0.55f ? 1 : 0) + (kind == CellKind::Courtyard ? 1 : 0);
                for (int r = 0; r < rubbleCount; ++r)
                {
                    const float sideOffset = (unitRand(eng) - 0.5f) * (data.cellSize * 0.80f);
                    const float depthOffset = 0.14f + unitRand(eng) * 0.42f;
                    const glm::vec3 rubblePos = wallFace + inward * depthOffset + tangent * sideOffset;
                    data.rubbles.push_back(
                        PropSpawn{rubblePos, glm::vec3(0.0f, 1.0f, 0.0f), yawRand(eng), rubbleScaleRand(eng)});
                }
            }

            if (kind == CellKind::Courtyard)
            {
                const int extraRocks = 2;
                for (int r = 0; r < extraRocks; ++r)
                {
                    const float ox = (unitRand(eng) - 0.5f) * (data.cellSize * 0.75f);
                    const float oz = (unitRand(eng) - 0.5f) * (data.cellSize * 0.75f);
                    const glm::vec3 cand = center + glm::vec3(ox, 0.0f, oz);
                    bool insideOasis = false;
                    for (const OasisSpawn &oasis : data.oases)
                    {
                        const glm::vec2 d(cand.x - oasis.center.x, cand.z - oasis.center.z);
                        const float maxR = std::max(oasis.radiusX, oasis.radiusZ) + 0.95f;
                        if (glm::dot(d, d) < maxR * maxR)
                        {
                            insideOasis = true;
                            break;
                        }
                    }
                    if (!insideOasis)
                    {
                        data.rubbles.push_back(PropSpawn{cand, glm::vec3(0.0f, 1.0f, 0.0f), yawRand(eng), rubbleScaleRand(eng)});
                    }
                }
            }
        }
    }
}

} // namespace

MazeMapData generateMaze(glm::ivec2 size, float cellSize, glm::ivec2 start, std::uint32_t seed)
{
    size.x = std::max(7, size.x | 1);
    size.y = std::max(7, size.y | 1);

    start.x = std::clamp(start.x | 1, 1, size.x - 2);
    start.y = std::clamp(start.y | 1, 1, size.y - 2);

    MazeMapData data{
        .size = size,
        .cellSize = cellSize,
        .cells = std::vector<CellKind>(static_cast<std::size_t>(size.x * size.y), CellKind::Wall),
        .startCell = start,
        .exitCell = start,
        .playerStart = glm::vec3(0.0f),
        .exitPos = glm::vec3(0.0f),
    };
    data.playerStart = data.cellCenterWorld(start);

    if (seed == 0)
    {
        std::random_device rd;
        seed = rd();
    }
    std::mt19937 eng(seed);

    const int maxSize = std::max(size.x, size.y);
    const int minCourtyards = (maxSize >= 19) ? 2 : 1;
    const int maxCourtyards = std::max(minCourtyards, (maxSize >= 21) ? 3 : (maxSize >> 3));
    std::uniform_int_distribution<> courtyardDis(minCourtyards, maxCourtyards);
    const int courtyardCount = courtyardDis(eng);

    std::vector<Courtyard> courtyards;
    for (int i = 0; i < courtyardCount; ++i)
    {
        fillCourtyard(data, start, eng, courtyards);
    }

    fillCorridor(data, start, eng);
    openCourtyard(data, eng, courtyards);
    resolveDeadEndsExitAndTreasures(data, start, eng);
    braidSpareDeadEnds(data, eng);
    populatePillarsAndBeams(data);
    populateOases(data, courtyards, eng);
    populateProps(data, eng);

    return data;
}

} // namespace game
