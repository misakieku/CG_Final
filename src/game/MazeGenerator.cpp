#include "game/MazeGenerator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <random>

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

void fillCorridor(MazeMapData &data, glm::ivec2 start, std::mt19937 &eng, const MazeGenDesc &desc)
{
    data.setCell(start, CellKind::Corridor);

    struct ActiveNode
    {
        glm::ivec2 cell{0};
        glm::ivec2 lastDir{0};
    };

    std::vector<ActiveNode> active;
    active.push_back({start, glm::ivec2(0)});

    std::array<glm::ivec2, 4> dirs = {
        glm::ivec2(-2, 0),
        glm::ivec2(0, 2),
        glm::ivec2(2, 0),
        glm::ivec2(0, -2),
    };

    std::uniform_real_distribution<float> unitRand(0.0f, 1.0f);
    const float newestBias = std::clamp(desc.corridorNewestBias, 0.0f, 1.0f);
    const float straightBias = std::clamp(desc.corridorStraightBias, 0.0f, 1.0f);
    const int maxRunBeforeBranch = std::max(1, desc.maxUnbranchedSteps);

    int stepsSinceBranch = 0;
    int totalCarved = 0;

    while (!active.empty())
    {
        std::size_t pickIdx = active.size() - 1u;
        const int branchCap = (totalCarved < 10) ? std::min(2, maxRunBeforeBranch) : maxRunBeforeBranch;
        const bool forceBranch = (active.size() > 1u && stepsSinceBranch >= branchCap);

        if (forceBranch || (active.size() > 1u && unitRand(eng) > newestBias))
        {
            pickIdx = std::uniform_int_distribution<std::size_t>(0u, active.size() - 2u)(eng);
            stepsSinceBranch = 0;
        }

        const ActiveNode current = active[pickIdx];
        std::shuffle(dirs.begin(), dirs.end(), eng);

        // Prefer continuing in the same direction to avoid 1-cell zig-zag corridors
        if (current.lastDir != glm::ivec2(0) && unitRand(eng) < straightBias)
        {
            for (std::size_t i = 1; i < dirs.size(); ++i)
            {
                if (dirs[i] == current.lastDir)
                {
                    std::swap(dirs[0], dirs[i]);
                    break;
                }
            }
        }

        bool carved = false;
        for (const glm::ivec2 dir : dirs)
        {
            const glm::ivec2 target = current.cell + dir;
            if (target.x > 0 && target.y > 0 && target.x < data.size.x - 1 && target.y < data.size.y - 1 &&
                data.getCell(target) == CellKind::Wall)
            {
                data.setCell(current.cell + dir / 2, CellKind::Corridor);
                data.setCell(target, CellKind::Corridor);
                active.push_back({target, dir});
                ++stepsSinceBranch;
                ++totalCarved;
                carved = true;
                break;
            }
        }

        if (!carved)
        {
            active.erase(active.begin() + static_cast<std::ptrdiff_t>(pickIdx));
            stepsSinceBranch = 0;
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

void resolveDeadEndsExitAndTreasures(MazeMapData &data, glm::ivec2 start, std::mt19937 &eng, const MazeGenDesc &desc)
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
    const int minTreasureSteps = std::max(desc.minTreasureStartSteps, maxDist / 4);
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
    const std::size_t maxTreasures = static_cast<std::size_t>(std::max(0, desc.maxTreasures));
    const int minSep = std::max(0, desc.minTreasureSeparation);
    std::vector<glm::ivec2> chosenCells;
    for (const glm::ivec2 cell : treasureCandidates)
    {
        if (chosenCells.size() >= maxTreasures)
        {
            break;
        }
        bool tooClose = false;
        for (const glm::ivec2 picked : chosenCells)
        {
            const int manhattan = std::abs(cell.x - picked.x) + std::abs(cell.y - picked.y);
            if (manhattan < minSep)
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
    const std::size_t minFallback = std::min(maxTreasures, std::min<std::size_t>(3, treasureCandidates.size()));
    for (const glm::ivec2 cell : treasureCandidates)
    {
        if (chosenCells.size() >= minFallback)
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

void addLongLoopShortcuts(MazeMapData &data, glm::ivec2 start, std::mt19937 &eng, int extraLoops)
{
    if (extraLoops <= 0)
    {
        return;
    }

    auto isProtected = [&](glm::ivec2 cell) noexcept -> bool
    {
        if (cell == data.startCell || cell == data.exitCell)
        {
            return true;
        }
        for (const glm::vec3 &tp : data.treasurePositions)
        {
            if (data.worldToCell(tp) == cell)
            {
                return true;
            }
        }
        return false;
    };

    auto countWalkableNeighbors = [&](glm::ivec2 cell) noexcept -> int
    {
        int count = 0;
        for (const glm::ivec2 d : kCardinalDirs)
        {
            if (data.isWalkable(cell + d))
            {
                ++count;
            }
        }
        return count;
    };

    struct ShortcutCandidate
    {
        glm::ivec2 wall{0};
        glm::ivec2 a{0};
        glm::ivec2 b{0};
    };

    std::vector<ShortcutCandidate> candidates;
    for (int y = 2; y < data.size.y - 2; ++y)
    {
        for (int x = 2; x < data.size.x - 2; ++x)
        {
            const glm::ivec2 wall{x, y};
            if (data.getCell(wall) != CellKind::Wall)
            {
                continue;
            }

            // Only break walls separating two opposite non-dead-end corridors with perpendicular walls intact
            if ((x & 1) == 0 && (y & 1) == 1)
            {
                const glm::ivec2 a{x - 1, y};
                const glm::ivec2 b{x + 1, y};
                if (data.getCell(a) == CellKind::Corridor && data.getCell(b) == CellKind::Corridor &&
                    !isProtected(a) && !isProtected(b) &&
                    countWalkableNeighbors(a) >= 2 && countWalkableNeighbors(b) >= 2 &&
                    data.getCell({x, y - 1}) == CellKind::Wall && data.getCell({x, y + 1}) == CellKind::Wall)
                {
                    candidates.push_back({wall, a, b});
                }
            }
            else if ((x & 1) == 1 && (y & 1) == 0)
            {
                const glm::ivec2 a{x, y - 1};
                const glm::ivec2 b{x, y + 1};
                if (data.getCell(a) == CellKind::Corridor && data.getCell(b) == CellKind::Corridor &&
                    !isProtected(a) && !isProtected(b) &&
                    countWalkableNeighbors(a) >= 2 && countWalkableNeighbors(b) >= 2 &&
                    data.getCell({x - 1, y}) == CellKind::Wall && data.getCell({x + 1, y}) == CellKind::Wall)
                {
                    candidates.push_back({wall, a, b});
                }
            }
        }
    }

    const std::vector<int> distFromStart = computeBfsDistances(data, start);
    std::shuffle(candidates.begin(), candidates.end(), eng);
    int opened = 0;
    for (const ShortcutCandidate &cand : candidates)
    {
        if (opened >= extraLoops)
        {
            break;
        }
        const std::vector<int> distFromA = computeBfsDistances(data, cand.a);
        const int sep = distFromA[static_cast<std::size_t>(data.getCellIndex(cand.b.x, cand.b.y))];
        const int startSep = std::min(
            distFromStart[static_cast<std::size_t>(data.getCellIndex(cand.a.x, cand.a.y))],
            distFromStart[static_cast<std::size_t>(data.getCellIndex(cand.b.x, cand.b.y))]);

        if (sep >= 10 && startSep >= 1)
        {
            data.setCell(cand.wall, CellKind::Corridor);
            ++opened;
        }
    }
}

void populatePillarsAndBeams(MazeMapData &data, const MazeGenDesc &desc)
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

    if (!desc.enableBeams)
    {
        return;
    }

    // Overhead corridor ceiling beams: scan contiguous straight passages and space beams evenly
    const int minRunCells = std::max(1, desc.minBeamRunCells);
    const float endMargin = std::max(0.0f, desc.beamEndMargin);
    const float beamSpacing = std::max(0.5f, desc.beamSpacing);

    const float minSepSq = (beamSpacing * 0.75f) * (beamSpacing * 0.75f);
    auto emitRunBeams = [&](int startIdx, int endIdx, bool verticalRun, int fixedCoord)
    {
        const int runCells = endIdx - startIdx + 1;
        if (runCells < minRunCells)
        {
            return;
        }

        const float spanStart = static_cast<float>(startIdx) * data.cellSize;
        const float spanEnd = static_cast<float>(endIdx + 1) * data.cellSize;
        const float usableStart = spanStart + endMargin;
        const float usableEnd = spanEnd - endMargin;
        const float usableLen = std::max(0.0f, usableEnd - usableStart);
        const int beamCount = 1 + static_cast<int>(std::floor(usableLen / beamSpacing));
        const float fixedWorld = (static_cast<float>(fixedCoord) + 0.5f) * data.cellSize;
        const float yaw = verticalRun ? 0.0f : 1.5707963f;

        for (int i = 0; i < beamCount; ++i)
        {
            const float coordWorld = (beamCount == 1)
                                         ? (spanStart + spanEnd) * 0.5f
                                         : (usableStart + (static_cast<float>(i) / static_cast<float>(beamCount - 1)) * usableLen);
            const glm::vec3 pos = verticalRun ? glm::vec3(fixedWorld, 0.0f, coordWorld) : glm::vec3(coordWorld, 0.0f, fixedWorld);
            bool tooClose = false;
            for (const PropSpawn &existing : data.ceilingBeams)
            {
                const glm::vec2 d(pos.x - existing.position.x, pos.z - existing.position.z);
                if (glm::dot(d, d) < minSepSq)
                {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose)
            {
                continue;
            }
            data.ceilingBeams.push_back(PropSpawn{
                .position = pos,
                .normal = glm::vec3(0.0f, -1.0f, 0.0f),
                .yaw = yaw,
                .scale = 1.0f,
            });
        }
    };

    // North-South straight corridors (walled East & West)
    for (int x = 1; x < data.size.x - 1; ++x)
    {
        int runStart = -1;
        for (int y = 1; y < data.size.y - 1; ++y)
        {
            const bool isStraightNS = data.getCell({x, y}) == CellKind::Corridor &&
                                      data.getCell({x - 1, y}) == CellKind::Wall &&
                                      data.getCell({x + 1, y}) == CellKind::Wall;
            if (isStraightNS && runStart < 0)
            {
                runStart = y;
            }
            else if (!isStraightNS && runStart >= 0)
            {
                emitRunBeams(runStart, y - 1, true, x);
                runStart = -1;
            }
        }
        if (runStart >= 0)
        {
            emitRunBeams(runStart, data.size.y - 2, true, x);
        }
    }

    // East-West straight corridors (walled North & South)
    for (int y = 1; y < data.size.y - 1; ++y)
    {
        int runStart = -1;
        for (int x = 1; x < data.size.x - 1; ++x)
        {
            const bool isStraightEW = data.getCell({x, y}) == CellKind::Corridor &&
                                      data.getCell({x, y - 1}) == CellKind::Wall &&
                                      data.getCell({x, y + 1}) == CellKind::Wall;
            if (isStraightEW && runStart < 0)
            {
                runStart = x;
            }
            else if (!isStraightEW && runStart >= 0)
            {
                emitRunBeams(runStart, x - 1, false, y);
                runStart = -1;
            }
        }
        if (runStart >= 0)
        {
            emitRunBeams(runStart, data.size.x - 2, false, y);
        }
    }
}

void populateOases(MazeMapData &data, const std::vector<Courtyard> &courtyards, std::mt19937 &eng, const MazeGenDesc &desc)
{
    if (!desc.enableOases)
    {
        return;
    }

    std::uniform_real_distribution<float> pushDist(0.55f, 0.95f);
    std::uniform_real_distribution<float> yawRand(0.0f, 6.2831853f);

    const float radiusX = std::max(0.5f, desc.oasisRadiusX);
    const float radiusZ = std::max(0.5f, desc.oasisRadiusZ);
    const float safeFootprintMargin = std::max(radiusX, radiusZ) + 2.55f;

    for (const Courtyard &c : courtyards)
    {
        if (c.size.x < desc.minOasisCourtyardSize || c.size.y < desc.minOasisCourtyardSize)
        {
            continue;
        }

        const float halfW = static_cast<float>(c.size.x) * data.cellSize * 0.5f;
        const float halfH = static_cast<float>(c.size.y) * data.cellSize * 0.5f;
        const float maxOffX = std::max(0.0f, halfW - safeFootprintMargin);
        const float maxOffZ = std::max(0.0f, halfH - safeFootprintMargin);

        const float angle = yawRand(eng);
        const float push = pushDist(eng);
        const float centerX = (static_cast<float>(c.start.x) + static_cast<float>(c.size.x) * 0.5f) * data.cellSize + std::cos(angle) * maxOffX * push;
        const float centerZ = (static_cast<float>(c.start.y) + static_cast<float>(c.size.y) * 0.5f) * data.cellSize + std::sin(angle) * maxOffZ * push;
        data.oases.push_back(OasisSpawn{
            .center = glm::vec3(centerX, 0.0f, centerZ),
            .radiusX = radiusX,
            .radiusZ = radiusZ,
            .yaw = yawRand(eng),
            .seed = eng(),
        });
    }
}

void populateProps(MazeMapData &data, std::mt19937 &eng, const MazeGenDesc &desc)
{
    std::uniform_real_distribution<float> unitRand(0.0f, 1.0f);
    std::uniform_real_distribution<float> yawRand(0.0f, 6.2831853f);
    const float rubbleMin = std::max(0.02f, desc.rubbleScaleMin);
    const float rubbleMax = std::max(rubbleMin, desc.rubbleScaleMax);
    std::uniform_real_distribution<float> rubbleScaleRand(rubbleMin, rubbleMax);

    const float candleMin = std::max(0.1f, desc.candleScaleMin);
    const float candleMax = std::max(candleMin, desc.candleScaleMax);
    std::uniform_real_distribution<float> candleScaleRand(candleMin, candleMax);

    const float halfCell = data.cellSize * 0.5f;
    const float minTorchDist = data.cellSize * std::max(0.25f, desc.torchMinSpacingCells);
    const float minTorchDistSq = minTorchDist * minTorchDist;

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

    struct ArchFootprint
    {
        glm::vec2 centerXZ{0.0f};
        glm::vec2 forwardXZ{0.0f, 1.0f};
    };

    std::vector<ArchFootprint> archFootprints;
    archFootprints.reserve(data.ceilingBeams.size() + 16u);
    for (const PropSpawn &beam : data.ceilingBeams)
    {
        archFootprints.push_back({
            .centerXZ = glm::vec2(beam.position.x, beam.position.z),
            .forwardXZ = glm::vec2(std::sin(beam.yaw), std::cos(beam.yaw)),
        });
    }
    for (int y = 1; y < data.size.y - 1; ++y)
    {
        for (int x = 1; x < data.size.x - 1; ++x)
        {
            if (data.getCell({x, y}) != CellKind::Courtyard)
            {
                continue;
            }
            const glm::vec3 center = data.cellCenterWorld({x, y});
            for (const glm::ivec2 d : kCardinalDirs)
            {
                const glm::ivec2 nb = glm::ivec2{x, y} + d;
                if (data.inBounds(nb) && data.getCell(nb) != CellKind::Courtyard && data.isWalkable(nb))
                {
                    const glm::vec3 outward(static_cast<float>(d.x), 0.0f, static_cast<float>(d.y));
                    const glm::vec3 gateCenter = center + outward * (halfCell + 0.22f);
                    archFootprints.push_back({
                        .centerXZ = glm::vec2(gateCenter.x, gateCenter.z),
                        .forwardXZ = glm::vec2(outward.x, outward.z),
                    });
                }
            }
        }
    }

    auto conflictsWithArchOrPillar = [&](const glm::vec3 &pos) -> bool {
        const glm::vec2 posXZ(pos.x, pos.z);
        const float maxAcross = data.cellSize * 0.65f;
        for (const ArchFootprint &arch : archFootprints)
        {
            const glm::vec2 delta = posXZ - arch.centerXZ;
            const glm::vec2 right(-arch.forwardXZ.y, arch.forwardXZ.x);
            const float along = std::abs(glm::dot(delta, arch.forwardXZ));
            const float across = std::abs(glm::dot(delta, right));
            if (along < 0.72f && across < maxAcross)
            {
                return true;
            }
        }
        constexpr float minPillarDistSq = 0.55f * 0.55f;
        for (const glm::vec3 &pillar : data.pillars)
        {
            const glm::vec2 d(pos.x - pillar.x, pos.z - pillar.z);
            if (glm::dot(d, d) < minPillarDistSq)
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

                // Wall torches (spaced by torchMinSpacingCells, avoiding arch pilasters and corner pillars)
                if (desc.enableTorches)
                {
                    const bool passChecker = !desc.torchCheckerFilter || (((x * 3 + y * 5) % 2) == 0);
                    const bool guaranteedSpot = shrineOrExit || cell == data.startCell;
                    if (guaranteedSpot || (passChecker && unitRand(eng) < desc.torchWallChance))
                    {
                        const float sideShift = (((x + y) & 1) == 0) ? 0.85f : -0.85f;
                        const std::array<float, 3> tangentOffsets{0.0f, sideShift, -sideShift};
                        const glm::vec3 baseTorchPos = wallFace + inward * 0.18f + glm::vec3(0.0f, desc.torchMountHeight, 0.0f);
                        for (const float off : tangentOffsets)
                        {
                            const glm::vec3 candPos = baseTorchPos + tangent * off;
                            if (!conflictsWithArchOrPillar(candPos) && !hasNearbyTorch(candPos))
                            {
                                data.wallTorches.push_back(PropSpawn{candPos, inward, wallYaw, 1.0f});
                                break;
                            }
                        }
                    }
                }

                // Floor candles along walls (guaranteed around treasure shrines & exit)
                if (desc.enableCandles)
                {
                    const int candleCount = shrineOrExit ? std::max(0, desc.shrineCandleClusters) : (unitRand(eng) < desc.candleWallChance ? 1 : 0);
                    for (int c = 0; c < candleCount; ++c)
                    {
                        const float sideOffset = (unitRand(eng) - 0.5f) * (data.cellSize * 0.55f);
                        const float depthOffset = 0.25f + unitRand(eng) * 0.18f;
                        const glm::vec3 candlePos = wallFace + inward * depthOffset + tangent * sideOffset;
                        data.candles.push_back(
                            PropSpawn{candlePos, glm::vec3(0.0f, 1.0f, 0.0f), yawRand(eng), candleScaleRand(eng)});
                    }
                }

                // Wall-edge rubble keeping the center path clear
                if (desc.enableRubble)
                {
                    const int rubbleCount = 1 + (unitRand(eng) < desc.rubbleExtraWallChance ? 1 : 0) + (kind == CellKind::Courtyard ? 1 : 0);
                    for (int r = 0; r < rubbleCount; ++r)
                    {
                        const float sideOffset = (unitRand(eng) - 0.5f) * (data.cellSize * 0.80f);
                        const float depthOffset = 0.14f + unitRand(eng) * 0.42f;
                        const glm::vec3 rubblePos = wallFace + inward * depthOffset + tangent * sideOffset;
                        data.rubbles.push_back(
                            PropSpawn{rubblePos, glm::vec3(0.0f, 1.0f, 0.0f), yawRand(eng), rubbleScaleRand(eng)});
                    }
                }
            }

            if (desc.enableRubble && kind == CellKind::Courtyard)
            {
                const int extraRocks = std::max(0, desc.courtyardExtraRocks);
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

MazeMapData generateMaze(const MazeGenDesc &desc)
{
    glm::ivec2 size{std::max(7, desc.size.x | 1), std::max(7, desc.size.y | 1)};
    glm::ivec2 start{std::clamp(desc.start.x | 1, 1, size.x - 2), std::clamp(desc.start.y | 1, 1, size.y - 2)};

    MazeMapData data{
        .size = size,
        .cellSize = desc.cellSize,
        .cells = std::vector<CellKind>(static_cast<std::size_t>(size.x * size.y), CellKind::Wall),
        .startCell = start,
        .exitCell = start,
        .playerStart = glm::vec3(0.0f),
        .exitPos = glm::vec3(0.0f),
    };
    data.playerStart = data.cellCenterWorld(start);

    std::uint32_t seed = desc.seed;
    if (seed == 0)
    {
        std::random_device rd;
        seed = rd();
    }
    std::mt19937 eng(seed);

    const int maxSize = std::max(size.x, size.y);
    const int minCourtyards = (desc.minCourtyards >= 0) ? desc.minCourtyards : ((maxSize >= 19) ? 2 : 1);
    const int autoMax = std::max(minCourtyards, (maxSize >= 21) ? 3 : (maxSize >> 3));
    const int maxCourtyards = (desc.maxCourtyards >= 0) ? std::max(minCourtyards, desc.maxCourtyards) : autoMax;
    std::uniform_int_distribution<> courtyardDis(minCourtyards, maxCourtyards);
    const int courtyardCount = courtyardDis(eng);

    std::vector<Courtyard> courtyards;
    for (int i = 0; i < courtyardCount; ++i)
    {
        fillCourtyard(data, start, eng, courtyards);
    }

    fillCorridor(data, start, eng, desc);
    openCourtyard(data, eng, courtyards);
    resolveDeadEndsExitAndTreasures(data, start, eng, desc);
    braidSpareDeadEnds(data, eng, std::clamp(desc.braidRatio, 0.0f, 1.0f));
    // addLongLoopShortcuts(data, start, eng, desc.extraLoopCount);
    populatePillarsAndBeams(data, desc);
    populateOases(data, courtyards, eng, desc);
    populateProps(data, eng, desc);

    return data;
}

} // namespace game
