#include "game/VineGenerator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

namespace game
{

namespace
{

struct VineAabb
{
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

struct VineNode
{
    glm::vec3 pos{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float radius = 0.018f;
    float hangWeight = 0.0f;
};

struct ChunkMeshData
{
    std::vector<render::SceneVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct SpatialAabbIndex
{
    int gridW = 1;
    int gridH = 1;
    float cellSize = 3.0f;
    std::vector<VineAabb> aabbs;
    std::vector<std::vector<std::uint32_t>> cellBuckets;

    void init(int w, int h, float cSize)
    {
        gridW = std::max(1, w);
        gridH = std::max(1, h);
        cellSize = cSize;
        cellBuckets.assign(static_cast<std::size_t>(gridW * gridH), {});
    }

    void addAabb(const VineAabb &box)
    {
        const auto id = static_cast<std::uint32_t>(aabbs.size());
        aabbs.push_back(box);

        // Expand lookup by 1 cell margin so nearby snapping is always found
        const int minX = std::clamp(static_cast<int>(std::floor(box.min.x / cellSize)) - 1, 0, gridW - 1);
        const int maxX = std::clamp(static_cast<int>(std::floor(box.max.x / cellSize)) + 1, 0, gridW - 1);
        const int minZ = std::clamp(static_cast<int>(std::floor(box.min.z / cellSize)) - 1, 0, gridH - 1);
        const int maxZ = std::clamp(static_cast<int>(std::floor(box.max.z / cellSize)) + 1, 0, gridH - 1);
        for (int z = minZ; z <= maxZ; ++z)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                cellBuckets[static_cast<std::size_t>(z * gridW + x)].push_back(id);
            }
        }
    }

    [[nodiscard]] const std::vector<std::uint32_t> &bucketAt(glm::vec3 p) const noexcept
    {
        const int cx = std::clamp(static_cast<int>(std::floor(p.x / cellSize)), 0, gridW - 1);
        const int cz = std::clamp(static_cast<int>(std::floor(p.z / cellSize)), 0, gridH - 1);
        return cellBuckets[static_cast<std::size_t>(cz * gridW + cx)];
    }
};

[[nodiscard]] SpatialAabbIndex buildSpatialAabbs(const MazeMapData &map, float wallHeight)
{
    SpatialAabbIndex spatial;
    spatial.init(map.size.x, map.size.y, map.cellSize);

    const float cell = map.cellSize;
    constexpr glm::ivec2 dirs[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};

    for (int y = 0; y < map.size.y; ++y)
    {
        for (int x = 0; x < map.size.x; ++x)
        {
            if (map.getCell({x, y}) != CellKind::Wall)
            {
                continue;
            }
            bool openAdj = false;
            for (const glm::ivec2 d : dirs)
            {
                if (map.isWalkable(glm::ivec2{x, y} + d))
                {
                    openAdj = true;
                    break;
                }
            }
            if (!openAdj)
            {
                continue;
            }
            const glm::vec3 minP(static_cast<float>(x) * cell, 0.0f, static_cast<float>(y) * cell);
            spatial.addAabb({minP, minP + glm::vec3(cell, wallHeight, cell)});
        }
    }

    // Corner pillars (base plinth + shaft + cap)
    for (const glm::vec3 &p : map.pillars)
    {
        spatial.addAabb({{p.x - 0.38f, 0.0f, p.z - 0.38f}, {p.x + 0.38f, 0.28f, p.z + 0.38f}});
        spatial.addAabb({{p.x - 0.27f, 0.0f, p.z - 0.27f}, {p.x + 0.27f, wallHeight, p.z + 0.27f}});
        spatial.addAabb({{p.x - 0.39f, wallHeight - 0.26f, p.z - 0.39f}, {p.x + 0.39f, wallHeight, p.z + 0.39f}});
    }

    // Overhead beams
    for (const PropSpawn &beam : map.ceilingBeams)
    {
        const bool alongZ = std::abs(std::sin(beam.yaw)) > 0.5f;
        const float hx = alongZ ? 0.21f : (cell * 0.48f);
        const float hz = alongZ ? (cell * 0.48f) : 0.21f;
        spatial.addAabb({
            glm::vec3(beam.position.x - hx, wallHeight - 0.34f, beam.position.z - hz),
            glm::vec3(beam.position.x + hx, wallHeight, beam.position.z + hz),
        });
    }

    return spatial;
}

[[nodiscard]] bool pushOutOfAabb(const VineAabb &box, glm::vec3 &pos, glm::vec3 &outNormal, float margin) noexcept
{
    const glm::vec3 bMin = box.min - glm::vec3(margin);
    const glm::vec3 bMax = box.max + glm::vec3(margin);
    if (pos.x <= bMin.x || pos.x >= bMax.x || pos.y <= bMin.y || pos.y >= bMax.y || pos.z <= bMin.z || pos.z >= bMax.z)
    {
        return false;
    }

    const float dxMin = pos.x - bMin.x;
    const float dxMax = bMax.x - pos.x;
    const float dyMin = pos.y - bMin.y;
    const float dyMax = bMax.y - pos.y;
    const float dzMin = pos.z - bMin.z;
    const float dzMax = bMax.z - pos.z;

    float best = dxMin;
    int axis = 0;
    float sign = -1.0f;

    auto check = [&](float dist, int ax, float sg)
    {
        if (dist < best)
        {
            best = dist;
            axis = ax;
            sign = sg;
        }
    };
    check(dxMax, 0, 1.0f);
    check(dyMin, 1, -1.0f);
    check(dyMax, 1, 1.0f);
    check(dzMin, 2, -1.0f);
    check(dzMax, 2, 1.0f);

    if (axis == 0)
    {
        pos.x = (sign < 0.0f) ? bMin.x : bMax.x;
        outNormal = glm::vec3(sign, 0.0f, 0.0f);
    }
    else if (axis == 1)
    {
        pos.y = (sign < 0.0f) ? bMin.y : bMax.y;
        outNormal = glm::vec3(0.0f, sign, 0.0f);
    }
    else
    {
        pos.z = (sign < 0.0f) ? bMin.z : bMax.z;
        outNormal = glm::vec3(0.0f, 0.0f, sign);
    }
    return true;
}

[[nodiscard]] bool snapToNearestSurface(const SpatialAabbIndex &spatial, glm::vec3 &pos, glm::vec3 &normal, float maxSnapDist, float offset) noexcept
{
    float bestDistSq = maxSnapDist * maxSnapDist;
    glm::vec3 bestPos = pos;
    glm::vec3 bestNormal = normal;
    bool found = false;

    // 1. Floor plane y = 0 is a first-class crawling surface (creates sprawling floor runners)
    if (pos.y >= -0.05f && pos.y <= maxSnapDist)
    {
        bestDistSq = pos.y * pos.y;
        bestPos = glm::vec3(pos.x, offset, pos.z);
        bestNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        found = true;
    }

    // 2. Nearby wall, pillar, and beam AABBs (strictly win ties near wall bases so vines climb upward!)
    for (const std::uint32_t boxIdx : spatial.bucketAt(pos))
    {
        const VineAabb &box = spatial.aabbs[boxIdx];
        const glm::vec3 clamped = glm::clamp(pos, box.min, box.max);
        const glm::vec3 delta = pos - clamped;
        const float d2 = glm::dot(delta, delta);

        // Bias slightly in favor of vertical walls/pillars near floor transitions
        const float weightedD2 = d2 * 0.85f;
        if (weightedD2 >= bestDistSq)
        {
            continue;
        }

        glm::vec3 faceN(0.0f);
        if (d2 > 1e-8f)
        {
            faceN = delta / std::sqrt(d2);
        }
        else
        {
            faceN = normal;
        }
        bestDistSq = weightedD2;
        bestPos = clamped + faceN * offset;
        bestNormal = faceN;
        found = true;
    }

    if (found)
    {
        pos = bestPos;
        normal = bestNormal;
    }
    return found;
}

[[nodiscard]] glm::vec3 makePerp(glm::vec3 n) noexcept
{
    const glm::vec3 ref = (std::abs(n.y) < 0.9f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    return glm::normalize(glm::cross(ref, n));
}

void emitIvyLeaf(ChunkMeshData &out, glm::vec3 anchor, glm::vec3 forwardDir, glm::vec3 leafNormal, float size, float shadeU, float hangWeight)
{
    const glm::vec3 f = glm::normalize(forwardDir - leafNormal * glm::dot(forwardDir, leafNormal) + glm::vec3(1e-4f));
    const glm::vec3 r = glm::normalize(glm::cross(f, leafNormal));
    const glm::vec3 n = glm::normalize(leafNormal);

    // Classic 3-lobed ivy leaf contour (6 boundary points + raised center fold)
    const glm::vec3 pCenter = anchor + f * (0.42f * size) + n * (0.07f * size);
    const std::array<glm::vec3, 6> rim = {
        anchor,
        anchor + r * (-0.52f * size) + f * (0.28f * size) - n * (0.03f * size),
        anchor + r * (-0.22f * size) + f * (0.50f * size),
        anchor + f * (1.00f * size) - n * (0.04f * size),
        anchor + r * (0.22f * size) + f * (0.50f * size),
        anchor + r * (0.52f * size) + f * (0.28f * size) - n * (0.03f * size),
    };

    auto emitTri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, float uCoord)
    {
        glm::vec3 triN = glm::cross(b - a, c - a);
        const float lenSq = glm::dot(triN, triN);
        triN = (lenSq > 1e-12f) ? (triN / std::sqrt(lenSq)) : n;
        const auto base = static_cast<std::uint32_t>(out.vertices.size());
        out.vertices.push_back({a, triN, {uCoord, hangWeight}});
        out.vertices.push_back({b, triN, {uCoord, hangWeight}});
        out.vertices.push_back({c, triN, {uCoord, hangWeight}});
        out.indices.push_back(base + 0);
        out.indices.push_back(base + 1);
        out.indices.push_back(base + 2);
    };

    for (std::size_t i = 0; i < 6; ++i)
    {
        const glm::vec3 &a = rim[i];
        const glm::vec3 &b = rim[(i + 1) % 6];
        // Front and back faces so leaves read cleanly under backface culling
        emitTri(pCenter, a, b, shadeU);
        emitTri(pCenter, b, a, std::max(0.30f, shadeU - 0.12f));
    }
}

void emitVineChain(ChunkMeshData &out, std::span<const VineNode> nodes, std::mt19937 &rng)
{
    if (nodes.size() < 2)
    {
        return;
    }

    constexpr int sides = 4;
    std::vector<std::uint32_t> ringStarts;
    ringStarts.reserve(nodes.size());

    glm::vec3 prevTangent = glm::normalize(nodes[1].pos - nodes[0].pos);
    glm::vec3 axisU = makePerp(prevTangent);
    glm::vec3 axisV = glm::normalize(glm::cross(prevTangent, axisU));

    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        glm::vec3 tangent = prevTangent;
        if (i + 1 < nodes.size())
        {
            const glm::vec3 d = nodes[i + 1].pos - nodes[i].pos;
            if (glm::dot(d, d) > 1e-8f)
            {
                tangent = glm::normalize(d);
            }
        }
        else
        {
            const glm::vec3 d = nodes[i].pos - nodes[i - 1].pos;
            if (glm::dot(d, d) > 1e-8f)
            {
                tangent = glm::normalize(d);
            }
        }

        axisU = axisU - tangent * glm::dot(axisU, tangent);
        const float uLen = glm::length(axisU);
        axisU = (uLen > 1e-4f) ? (axisU / uLen) : makePerp(tangent);
        axisV = glm::normalize(glm::cross(tangent, axisU));
        prevTangent = tangent;

        const auto ringBase = static_cast<std::uint32_t>(out.vertices.size());
        ringStarts.push_back(ringBase);

        for (int s = 0; s < sides; ++s)
        {
            const float ang = static_cast<float>(s) * 1.5707963f;
            const glm::vec3 n = std::cos(ang) * axisU + std::sin(ang) * axisV;
            const glm::vec3 p = nodes[i].pos + n * nodes[i].radius;
            out.vertices.push_back({p, n, {0.10f, nodes[i].hangWeight}});
        }
    }

    for (std::size_t i = 0; i + 1 < nodes.size(); ++i)
    {
        const std::uint32_t r0 = ringStarts[i];
        const std::uint32_t r1 = ringStarts[i + 1];
        for (int s = 0; s < sides; ++s)
        {
            const std::uint32_t s0 = static_cast<std::uint32_t>(s);
            const std::uint32_t s1 = static_cast<std::uint32_t>((s + 1) % sides);
            out.indices.push_back(r0 + s0);
            out.indices.push_back(r1 + s0);
            out.indices.push_back(r1 + s1);
            out.indices.push_back(r0 + s0);
            out.indices.push_back(r1 + s1);
            out.indices.push_back(r0 + s1);
        }
    }

    // Dense staggered 3-lobed ivy leaves hugging the surface and overlapping each other
    std::uniform_real_distribution<float> sizeDist(0.11f, 0.22f);
    std::uniform_real_distribution<float> shadeDist(0.45f, 0.98f);
    std::uniform_real_distribution<float> tiltDist(0.15f, 0.55f);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    for (std::size_t i = 1; i < nodes.size(); ++i)
    {
        const glm::vec3 stemDir = glm::normalize(nodes[i].pos - nodes[i - 1].pos);
        const glm::vec3 surfN = nodes[i].normal;
        glm::vec3 side = glm::cross(surfN, stemDir);
        if (glm::dot(side, side) < 1e-4f)
        {
            side = makePerp(stemDir);
        }
        else
        {
            side = glm::normalize(side);
        }

        const int leavesHere = (chance(rng) < 0.65f) ? 2 : 1;
        for (int l = 0; l < leavesHere; ++l)
        {
            const float sideSign = ((static_cast<int>(i) + l) % 2 == 0) ? 1.0f : -1.0f;
            const float taper = 1.0f - 0.35f * (static_cast<float>(i) / static_cast<float>(nodes.size()));
            const float leafSize = sizeDist(rng) * taper;
            const glm::vec3 attachPos = glm::mix(nodes[i - 1].pos, nodes[i].pos, 0.25f + 0.55f * chance(rng)) +
                                        side * (sideSign * nodes[i].radius * 0.8f) + surfN * 0.015f;
            const glm::vec3 leafDir = glm::normalize(side * (sideSign * (0.75f + 0.35f * chance(rng))) +
                                                     stemDir * (0.55f * (chance(rng) - 0.15f)) +
                                                     surfN * (0.18f * chance(rng)));
            const glm::vec3 leafNorm = glm::normalize(surfN + side * (sideSign * tiltDist(rng) * 0.45f));

            emitIvyLeaf(out, attachPos, leafDir, leafNorm, leafSize, shadeDist(rng), nodes[i].hangWeight);
        }
    }
}

void growIvyBranch(const SpatialAabbIndex &spatial, ChunkMeshData &chunk, glm::vec3 startPos, glm::vec3 startNormal, glm::vec3 startDir, glm::vec3 attractPoint, int steps, float climbBias, bool startHanging, std::mt19937 &rng, int depth)
{
    std::uniform_real_distribution<float> jitter(-1.0f, 1.0f);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    std::vector<VineNode> nodes;
    nodes.reserve(static_cast<std::size_t>(steps + 1));

    glm::vec3 pos = startPos;
    glm::vec3 normal = glm::normalize(startNormal);
    glm::vec3 dir = glm::normalize(startDir);
    bool hanging = startHanging;
    int hangStep = 0;

    const float baseRadius = (depth == 0) ? 0.020f : 0.012f;
    constexpr float stepLen = 0.16f;
    constexpr float surfaceOffset = 0.024f;

    nodes.push_back(VineNode{pos, normal, baseRadius, 0.0f});

    for (int s = 1; s <= steps; ++s)
    {
        const float t = static_cast<float>(s) / static_cast<float>(steps);

        if (hanging)
        {
            const glm::vec3 wander(jitter(rng) * 0.28f, 0.0f, jitter(rng) * 0.28f);
            dir = glm::normalize(dir * 0.62f + glm::vec3(0.0f, -0.58f, 0.0f) + wander);
            pos += dir * stepLen;
            for (const std::uint32_t boxIdx : spatial.bucketAt(pos))
            {
                if (pushOutOfAabb(spatial.aabbs[boxIdx], pos, normal, surfaceOffset))
                {
                    dir = glm::normalize(dir + normal * 0.30f);
                }
            }
            ++hangStep;
        }
        else
        {
            // Project previous direction onto current surface tangent plane
            glm::vec3 tangent = dir - normal * glm::dot(dir, normal);
            if (glm::dot(tangent, tangent) < 1e-4f)
            {
                tangent = makePerp(normal);
            }
            tangent = glm::normalize(tangent);

            const glm::vec3 side = glm::normalize(glm::cross(normal, tangent));
            const glm::vec3 toTarget = attractPoint - pos;
            const glm::vec3 tangentToTarget = toTarget - normal * glm::dot(toTarget, normal);
            const glm::vec3 pull = (glm::dot(tangentToTarget, tangentToTarget) > 1e-4f)
                                       ? glm::normalize(tangentToTarget)
                                       : glm::vec3(0.0f);

            const glm::vec3 upVec(0.0f, 1.0f, 0.0f);
            const glm::vec3 tangentUp = upVec - normal * glm::dot(upVec, normal);

            // Classic Ivy weighting: momentum + upward/target tendency + sinuous wander
            const float onFloor = (normal.y > 0.7f) ? 1.0f : 0.0f;
            glm::vec3 desired = tangent * 0.68f + side * (jitter(rng) * 0.42f);
            desired += pull * (onFloor * 0.42f);
            desired += tangentUp * ((1.0f - onFloor) * climbBias);

            if (glm::dot(desired, desired) > 1e-6f)
            {
                dir = glm::normalize(desired);
            }
            pos += dir * stepLen;

            // Push out of any penetrated AABBs and wrap normal around corners
            for (const std::uint32_t boxIdx : spatial.bucketAt(pos))
            {
                if (pushOutOfAabb(spatial.aabbs[boxIdx], pos, normal, surfaceOffset))
                {
                    dir = dir - normal * glm::dot(dir, normal);
                    if (glm::dot(dir, dir) > 1e-4f)
                    {
                        dir = glm::normalize(dir);
                    }
                }
            }

            if (!snapToNearestSurface(spatial, pos, normal, 0.24f, surfaceOffset))
            {
                hanging = true;
            }
        }

        if (pos.y < 0.02f)
        {
            pos.y = surfaceOffset;
        }
        if (hanging && pos.y < 0.20f)
        {
            break;
        }

        const float hangWeight = hanging ? std::clamp(static_cast<float>(hangStep) / 5.0f, 0.0f, 1.0f) : 0.0f;
        const float radius = baseRadius * (1.0f - 0.68f * t);
        nodes.push_back(VineNode{pos, normal, radius, hangWeight});

        // Frequent short lateral branches create the dense interwoven ivy mat
        if (depth == 0 && s >= 3 && s + 5 < steps && chance(rng) < 0.28f)
        {
            const glm::vec3 side = glm::cross(normal, dir);
            const float sideLenSq = glm::dot(side, side);
            const glm::vec3 sideAxis = (sideLenSq > 1e-4f) ? (side / std::sqrt(sideLenSq)) : makePerp(dir);
            const float sign = (chance(rng) < 0.5f) ? 1.0f : -1.0f;
            const glm::vec3 branchDir = glm::normalize(dir * 0.55f + sideAxis * (sign * 0.85f));
            const int branchLen = 8 + static_cast<int>(chance(rng) * 9.0f);
            growIvyBranch(spatial, chunk, pos, normal, branchDir, attractPoint, branchLen, climbBias * 0.7f, hanging, rng, 1);
        }
    }

    emitVineChain(chunk, nodes, rng);
}

} // namespace

void generateDungeonVines(const MazeMapData &map, render::MegaScene &scene, std::uint32_t materialId, float wallHeight, std::uint32_t seed)
{
    const SpatialAabbIndex spatial = buildSpatialAabbs(map, wallHeight);
    if (spatial.aabbs.empty())
    {
        return;
    }

    constexpr int kChunkCells = 4;
    const int chunksX = (map.size.x + kChunkCells - 1) / kChunkCells;
    const int chunksZ = (map.size.y + kChunkCells - 1) / kChunkCells;
    std::vector<ChunkMeshData> chunks(static_cast<std::size_t>(chunksX * chunksZ));

    auto chunkForWorld = [&](glm::vec3 p) -> ChunkMeshData &
    {
        const int cx = std::clamp(static_cast<int>(std::floor(p.x / map.cellSize)) / kChunkCells, 0, chunksX - 1);
        const int cz = std::clamp(static_cast<int>(std::floor(p.z / map.cellSize)) / kChunkCells, 0, chunksZ - 1);
        return chunks[static_cast<std::size_t>(cz * chunksX + cx)];
    };

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> angleDist(0.0f, 6.2831853f);
    std::uniform_int_distribution<int> climbSteps(20, 34);
    std::uniform_int_distribution<int> hangSteps(7, 13);

    // 1. Floor-sprawling & pillar-climbing ivy clusters at selected corner pillars
    for (const glm::vec3 &pillar : map.pillars)
    {
        if (unit(rng) > 0.36f)
        {
            continue;
        }
        const int mainStems = 2 + (unit(rng) < 0.45f ? 1 : 0);
        ChunkMeshData &chunk = chunkForWorld(pillar);
        const glm::vec3 attract(pillar.x, wallHeight * 0.85f, pillar.z);

        for (int k = 0; k < mainStems; ++k)
        {
            const float a = angleDist(rng);
            const glm::vec3 radial(std::cos(a), 0.0f, std::sin(a));
            const float startDist = 0.45f + unit(rng) * 0.55f;
            glm::vec3 seedPos = pillar + radial * startDist + glm::vec3(0.0f, 0.024f, 0.0f);
            glm::vec3 normal(0.0f, 1.0f, 0.0f);

            // Only keep floor root if it lies on walkable floor (not buried in an adjacent wall)
            bool insideWall = false;
            for (const std::uint32_t boxIdx : spatial.bucketAt(seedPos))
            {
                if (pushOutOfAabb(spatial.aabbs[boxIdx], seedPos, normal, 0.024f))
                {
                    insideWall = true;
                }
            }
            if (insideWall && seedPos.y > 0.35f)
            {
                continue;
            }
            seedPos.y = 0.024f;
            normal = glm::vec3(0.0f, 1.0f, 0.0f);

            const glm::vec3 initDir = glm::normalize(-radial + glm::vec3(-radial.z, 0.0f, radial.x) * (unit(rng) - 0.5f));
            growIvyBranch(spatial, chunk, seedPos, normal, initDir, attract, climbSteps(rng), 0.48f, false, rng, 0);
        }
    }

    // 2. Courtyard wall-base climbing & upper-rim draping ivy
    const float halfCell = map.cellSize * 0.5f;
    constexpr glm::ivec2 dirs[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (int y = 1; y < map.size.y - 1; ++y)
    {
        for (int x = 1; x < map.size.x - 1; ++x)
        {
            const CellKind kind = map.getCell({x, y});
            if (kind == CellKind::Wall)
            {
                continue;
            }
            const glm::vec3 center = map.cellCenterWorld({x, y});
            const bool isCourtyard = (kind == CellKind::Courtyard);

            for (const glm::ivec2 d : dirs)
            {
                const glm::ivec2 nb = glm::ivec2{x, y} + d;
                if (map.inBounds(nb) && map.getCell(nb) != CellKind::Wall)
                {
                    continue;
                }

                const glm::vec3 outward(static_cast<float>(d.x), 0.0f, static_cast<float>(d.y));
                const glm::vec3 inward = -outward;
                const glm::vec3 tangent(-inward.z, 0.0f, inward.x);

                // Floor-to-wall climbing runners
                if (unit(rng) < (isCourtyard ? 0.55f : 0.14f))
                {
                    const float sideOff = (unit(rng) - 0.5f) * (map.cellSize * 0.60f);
                    const glm::vec3 wallBase = center + outward * halfCell + tangent * sideOff;
                    const glm::vec3 floorRoot = wallBase + inward * (0.45f + 0.55f * unit(rng)) + glm::vec3(0.0f, 0.024f, 0.0f);
                    const glm::vec3 attract = wallBase + inward * 0.03f + glm::vec3(0.0f, wallHeight * 0.8f, 0.0f);
                    ChunkMeshData &chunk = chunkForWorld(floorRoot);

                    const int stems = isCourtyard ? 2 : 1;
                    for (int k = 0; k < stems; ++k)
                    {
                        const glm::vec3 dir = glm::normalize(outward + tangent * ((unit(rng) - 0.5f) * 1.1f));
                        growIvyBranch(spatial, chunk, floorRoot, {0.0f, 1.0f, 0.0f}, dir, attract, climbSteps(rng), 0.42f, false, rng, 0);
                    }
                }

                // Courtyard rim top-down hanging ivy curtains
                if (isCourtyard && unit(rng) < 0.45f)
                {
                    const float sideOff = (unit(rng) - 0.5f) * (map.cellSize * 0.65f);
                    const glm::vec3 rimPos = center + outward * halfCell + inward * 0.04f + tangent * sideOff + glm::vec3(0.0f, wallHeight - 0.08f, 0.0f);
                    ChunkMeshData &chunk = chunkForWorld(rimPos);
                    const int tendrils = 2;
                    for (int k = 0; k < tendrils; ++k)
                    {
                        const glm::vec3 dir = glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f) + tangent * ((unit(rng) - 0.5f) * 0.7f));
                        growIvyBranch(spatial, chunk, rimPos, inward, dir, rimPos - glm::vec3(0, 2, 0), hangSteps(rng), -0.35f, true, rng, 0);
                    }
                }
            }
        }
    }

    // 3. Ceiling beam draping ivy clusters
    for (const PropSpawn &beam : map.ceilingBeams)
    {
        if (unit(rng) > 0.35f)
        {
            continue;
        }
        const glm::vec3 along(std::cos(beam.yaw), 0.0f, -std::sin(beam.yaw));
        const glm::vec3 side(-along.z, 0.0f, along.x);
        const float offsetAlong = (unit(rng) - 0.5f) * (map.cellSize * 0.5f);
        const float sideSign = (unit(rng) < 0.5f) ? -1.0f : 1.0f;
        const glm::vec3 seedPos = beam.position + along * offsetAlong + side * (sideSign * 0.22f) + glm::vec3(0.0f, wallHeight - 0.34f, 0.0f);

        ChunkMeshData &chunk = chunkForWorld(seedPos);
        for (int k = 0; k < 2; ++k)
        {
            const glm::vec3 dir = glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f) + along * ((unit(rng) - 0.5f) * 0.6f));
            growIvyBranch(spatial, chunk, seedPos, side * sideSign, dir, seedPos - glm::vec3(0, 2, 0), hangSteps(rng), -0.3f, true, rng, 0);
        }
    }

    const std::uint32_t vineFlags = render::kObjectVisibleFlag | render::kObjectWindSwayFlag;
    for (const ChunkMeshData &chunk : chunks)
    {
        if (chunk.vertices.empty() || chunk.indices.empty())
        {
            continue;
        }
        const std::uint32_t meshId = scene.registerMesh(chunk.vertices, chunk.indices);
        scene.addObject(meshId, materialId, glm::mat4(1.0f), vineFlags);
    }
}

} // namespace game
