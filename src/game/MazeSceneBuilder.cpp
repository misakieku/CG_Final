#include "game/MazeSceneBuilder.hpp"
#include "game/FoliageGenerator.hpp"
#include "game/ProceduralStone.hpp"
#include "game/VineGenerator.hpp"

#include "common/dat_mesh.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <span>

namespace game
{

namespace
{

constexpr std::uint32_t kMatFloor = 0;
constexpr std::uint32_t kMatStoneWall = 1;
constexpr std::uint32_t kMatGoldAccent = 2;
constexpr std::uint32_t kMatVineMoss = 3;
constexpr std::uint32_t kMatBushFoliage = 7;
constexpr std::uint32_t kMatWoodPlank = 8;
constexpr std::uint32_t kMatTorch = 9;

struct LoadedSorModel
{
    std::uint32_t meshId = 0;
    float minY = -0.5f;
    float height = 1.0f;
    float maxRadiusXZ = 0.5f;
    bool loaded = false;

    [[nodiscard]] float fitScale(float targetHeight, float maxAllowedRadius) const noexcept
    {
        const float byHeight = targetHeight / std::max(0.05f, height);
        const float byRadius = maxAllowedRadius / std::max(0.05f, maxRadiusXZ);
        return std::min(byHeight, byRadius);
    }
};

[[nodiscard]] LoadedSorModel tryLoadSorModel(render::MegaScene &scene, const std::filesystem::path &path, std::uint32_t fallbackMeshId)
{
    LoadedSorModel result;
    result.meshId = fallbackMeshId;
    if (!std::filesystem::exists(path))
    {
        return result;
    }

    const cg::DatMeshData dat = cg::loadDatMesh(path);
    if (dat.positions.empty() || dat.indices.empty())
    {
        return result;
    }

    float minY = dat.positions[0].y;
    float maxY = dat.positions[0].y;
    float maxRadSq = 0.0f;
    for (const glm::vec3 &p : dat.positions)
    {
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
        maxRadSq = std::max(maxRadSq, p.x * p.x + p.z * p.z);
    }

    result.meshId = scene.registerDatMesh(dat);
    result.minY = minY;
    result.height = std::max(0.05f, maxY - minY);
    result.maxRadiusXZ = std::max(0.05f, std::sqrt(maxRadSq));
    result.loaded = true;
    return result;
}

[[nodiscard]] bool hasOpenNeighbor(const MazeMapData &map, int x, int y) noexcept
{
    constexpr glm::ivec2 dirs[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (const glm::ivec2 d : dirs)
    {
        if (map.isWalkable(glm::ivec2{x, y} + d))
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] glm::mat4 makeTransform(glm::vec3 pos, float yaw, glm::vec3 scale) noexcept
{
    glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
    if (std::abs(yaw) > 1e-5f)
    {
        m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    return glm::scale(m, scale);
}

void buildCourtyardParapets(const MazeMapData &map, render::MegaScene &scene, std::uint32_t bevelBoxMesh, std::span<const std::uint32_t> rockMeshes, std::span<const std::uint32_t> archMeshes, std::uint32_t stoneMat, std::uint32_t worldFlags, std::uint32_t stoneFlags, float wallHeight)
{
    const float cell = map.cellSize;
    const float halfCell = cell * 0.5f;
    constexpr glm::ivec2 dirs[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};

    std::mt19937 rng(918273u);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    for (int y = 1; y < map.size.y - 1; ++y)
    {
        for (int x = 1; x < map.size.x - 1; ++x)
        {
            if (map.getCell({x, y}) != CellKind::Courtyard)
            {
                continue;
            }

            const glm::vec3 center = map.cellCenterWorld({x, y});
            for (const glm::ivec2 d : dirs)
            {
                const glm::ivec2 neighbor = glm::ivec2{x, y} + d;
                const glm::vec3 outward(static_cast<float>(d.x), 0.0f, static_cast<float>(d.y));
                const glm::vec3 inward = -outward;
                const glm::vec3 tangent(-inward.z, 0.0f, inward.x);
                const glm::vec3 wallFace = center + outward * halfCell;
                const float wallYaw = std::atan2(inward.x, inward.z);

                if (map.inBounds(neighbor) && map.getCell(neighbor) != CellKind::Wall)
                {
                    // Courtyard-to-corridor portal: carved archway, heavy lintel sheathing the roof slab edge, and stepped gatehouse crown
                    if (map.getCell(neighbor) != CellKind::Courtyard && map.isWalkable(neighbor))
                    {
                        const std::uint32_t archId = archMeshes[static_cast<std::size_t>(x + y) % archMeshes.size()];
                        const glm::vec3 gateCenter = wallFace - inward * 0.22f;
                        scene.addObject(archId, stoneMat, makeTransform(gateCenter, wallYaw, glm::vec3(1.0f)), stoneFlags);

                        // Heavy beveled stone lintel wrapping the exposed roof edge & aligning with flanking wall cornices
                        const glm::vec3 lintelPos = wallFace - inward * 0.14f + glm::vec3(0.0f, wallHeight - 0.02f, 0.0f);
                        scene.addObject(bevelBoxMesh, stoneMat, makeTransform(lintelPos, wallYaw, glm::vec3(cell * 0.96f, 0.34f, 0.56f)), worldFlags);

                        // Stepped gatehouse pediment + flanking merlons matching courtyard parapet silhouette
                        const glm::vec3 pedimentPos = gateCenter + glm::vec3(0.0f, wallHeight + 0.24f, 0.0f);
                        scene.addObject(bevelBoxMesh, stoneMat, makeTransform(pedimentPos, wallYaw, glm::vec3(1.26f, 0.22f, 0.50f)), worldFlags);
                        const glm::vec3 crestPos = gateCenter + glm::vec3(0.0f, wallHeight + 0.46f, 0.0f);
                        scene.addObject(bevelBoxMesh, stoneMat, makeTransform(crestPos, wallYaw, glm::vec3(0.56f, 0.28f, 0.54f)), worldFlags);

                        for (const float side : {-1.0f, 1.0f})
                        {
                            const float h = 0.26f + unit(rng) * 0.14f;
                            const glm::vec3 mPos = gateCenter + tangent * (side * 0.92f) + glm::vec3(0.0f, wallHeight + h * 0.5f + 0.06f, 0.0f);
                            scene.addObject(bevelBoxMesh, stoneMat, makeTransform(mPos, wallYaw + (unit(rng) - 0.5f) * 0.04f, glm::vec3(0.52f, h, 0.46f)), worldFlags);
                        }
                    }
                    continue;
                }

                // Protruding stone cornice trim breaking the razor-sharp top wall edge
                const glm::vec3 cornicePos = wallFace - inward * 0.10f + glm::vec3(0.0f, wallHeight - 0.10f, 0.0f);
                scene.addObject(bevelBoxMesh, stoneMat, makeTransform(cornicePos, wallYaw, glm::vec3(cell + 0.06f, 0.22f, 0.48f)), worldFlags);

                // Two ruined merlon/capstone slots along each 3.0m courtyard wall edge
                for (int slot = 0; slot < 2; ++slot)
                {
                    const float baseOff = (slot == 0 ? -0.74f : 0.74f) + (unit(rng) - 0.5f) * 0.14f;
                    const float roll = unit(rng);
                    if (roll < 0.44f)
                    {
                        // Tall weathered battlement merlon
                        const float w = 0.86f + unit(rng) * 0.30f;
                        const float h = 0.40f + unit(rng) * 0.26f;
                        const float depth = 0.48f + unit(rng) * 0.16f;
                        const glm::vec3 p = wallFace - inward * 0.26f + tangent * baseOff + glm::vec3(0.0f, wallHeight + h * 0.5f - 0.04f, 0.0f);
                        scene.addObject(bevelBoxMesh, stoneMat, makeTransform(p, wallYaw + (unit(rng) - 0.5f) * 0.06f, glm::vec3(w, h, depth)), worldFlags);
                    }
                    else if (roll < 0.74f)
                    {
                        // Half-broken low parapet stone block
                        const float w = 0.72f + unit(rng) * 0.32f;
                        const float h = 0.16f + unit(rng) * 0.15f;
                        const float depth = 0.44f + unit(rng) * 0.16f;
                        const glm::vec3 p = wallFace - inward * 0.25f + tangent * baseOff + glm::vec3(0.0f, wallHeight + h * 0.5f - 0.03f, 0.0f);
                        scene.addObject(bevelBoxMesh, stoneMat, makeTransform(p, wallYaw + (unit(rng) - 0.5f) * 0.10f, glm::vec3(w, h, depth)), worldFlags);
                    }
                    else if (!rockMeshes.empty() && unit(rng) < 0.70f)
                    {
                        // Collapsed gap with weathered rubble perched on the broken ledge
                        const int count = (unit(rng) < 0.45f) ? 2 : 1;
                        for (int r = 0; r < count; ++r)
                        {
                            const float s = 0.14f + unit(rng) * 0.11f;
                            const float rOff = baseOff + (unit(rng) - 0.5f) * 0.45f;
                            const glm::vec3 p = wallFace - inward * 0.22f + tangent * rOff + glm::vec3(0.0f, wallHeight + s * 0.25f, 0.0f);
                            const std::uint32_t rockId = rockMeshes[static_cast<std::size_t>((x + y + slot + r)) % rockMeshes.size()];
                            scene.addObject(rockId, stoneMat, makeTransform(p, unit(rng) * 6.283f, glm::vec3(s * 1.2f, s * 0.85f, s)), worldFlags);
                        }
                    }
                }
            }
        }
    }

    // Stepped finial capstones on pillars bordering open courtyards
    for (const glm::vec3 &p : map.pillars)
    {
        const int vx = static_cast<int>(std::round(p.x / cell));
        const int vy = static_cast<int>(std::round(p.z / cell));
        bool touchesCourtyard = false;
        for (int dy = -1; dy <= 0 && !touchesCourtyard; ++dy)
        {
            for (int dx = -1; dx <= 0; ++dx)
            {
                const glm::ivec2 c{vx + dx, vy + dy};
                if (map.inBounds(c) && map.getCell(c) == CellKind::Courtyard)
                {
                    touchesCourtyard = true;
                    break;
                }
            }
        }
        if (touchesCourtyard)
        {
            scene.addObject(bevelBoxMesh, stoneMat, makeTransform(p + glm::vec3(0.0f, wallHeight + 0.16f, 0.0f), 0.0f, glm::vec3(0.66f, 0.34f, 0.66f)), worldFlags);
        }
    }
}

void buildTempleWallTrim(const MazeMapData &map, render::MegaScene &scene, std::span<const std::uint32_t> plinthMeshes, std::span<const std::uint32_t> dadoMeshes, std::span<const std::uint32_t> architraveMeshes, std::uint32_t bevelBoxMesh, std::uint32_t stoneMat, std::uint32_t worldFlags, float wallHeight)
{
    const float cell = map.cellSize;
    const float halfCell = cell * 0.5f;
    constexpr glm::ivec2 dirs[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};

    for (int y = 1; y < map.size.y - 1; ++y)
    {
        for (int x = 1; x < map.size.x - 1; ++x)
        {
            const glm::ivec2 cellCoord{x, y};
            if (!map.isWalkable(cellCoord))
            {
                continue;
            }

            const glm::vec3 center = map.cellCenterWorld(cellCoord);
            for (std::size_t dIdx = 0; dIdx < 4; ++dIdx)
            {
                const glm::ivec2 d = dirs[dIdx];
                const glm::ivec2 neighbor = cellCoord + d;
                if (map.inBounds(neighbor) && map.getCell(neighbor) != CellKind::Wall)
                {
                    continue;
                }

                const glm::vec3 outward(static_cast<float>(d.x), 0.0f, static_cast<float>(d.y));
                const glm::vec3 inward = -outward;
                const glm::vec3 wallFace = center + outward * halfCell;
                const float wallYaw = std::atan2(inward.x, inward.z);
                const std::size_t var = static_cast<std::size_t>(x * 37 + y * 91 + static_cast<int>(dIdx));

                // Segmented chipped stone plinth hugging wall base
                scene.addObject(plinthMeshes[var % plinthMeshes.size()], stoneMat, makeTransform(wallFace + inward * 0.045f + glm::vec3(0.0f, 0.10f, 0.0f), wallYaw, glm::vec3(1.0f)), worldFlags);

                // Segmented chipped chest-height dado stringcourse + upper chamfer lip
                scene.addObject(dadoMeshes[var % dadoMeshes.size()], stoneMat, makeTransform(wallFace + inward * 0.025f + glm::vec3(0.0f, 1.00f, 0.0f), wallYaw, glm::vec3(1.0f)), worldFlags);
                scene.addObject(bevelBoxMesh, stoneMat, makeTransform(wallFace + inward * 0.032f + glm::vec3(0.0f, 1.08f, 0.0f), wallYaw, glm::vec3(cell + 0.02f, 0.045f, 0.13f)), worldFlags & ~render::kObjectStoneDetailFlag);

                // Segmented upper architrave frieze ledge
                scene.addObject(architraveMeshes[(var + 1) % architraveMeshes.size()], stoneMat, makeTransform(wallFace + inward * 0.030f + glm::vec3(0.0f, wallHeight - 0.50f, 0.0f), wallYaw, glm::vec3(1.0f)), worldFlags);
            }
        }
    }
}

MazeSceneHandles populateMazeSceneCpu(
    const MazeMapData &map, render::MegaScene &scene, const std::filesystem::path &exeDir, const MazeSceneDesc &desc,
    std::vector<render::GrassInstance> *grassInstancesOut, std::atomic<int> *stageOut)
{
    auto setStage = [&](int s)
    {
        if (stageOut != nullptr)
        {
            stageOut->store(s, std::memory_order_relaxed);
        }
    };
    using namespace render;

    setStage(2);
    std::vector<FloorObstacleObb> floorObstacles;
    const float wallHeight = desc.wallHeight;
    const float cell = map.cellSize;
    const float totalW = static_cast<float>(map.size.x) * cell;
    const float totalH = static_cast<float>(map.size.y) * cell;

    const std::uint32_t floorMesh = registerPlaneMesh(scene, 0.5f, 1.0f);
    const std::uint32_t boxMesh = registerUnitBoxMesh(scene, 0.5f, 1.0f);
    const std::uint32_t bevelBoxMesh = registerBeveledBoxMesh(scene, 0.5f, 0.045f);
    const std::array<std::uint32_t, 3> rockMeshes = {
        registerRockMesh(scene, 101u),
        registerRockMesh(scene, 202u),
        registerRockMesh(scene, 303u),
    };

    const std::array<std::uint32_t, 4> wallVariantMeshes = {
        buildChippedWallVariantMesh(scene, cell, wallHeight, 1001u),
        buildChippedWallVariantMesh(scene, cell, wallHeight, 2003u),
        buildChippedWallVariantMesh(scene, cell, wallHeight, 3007u),
        buildChippedWallVariantMesh(scene, cell, wallHeight, 4011u),
    };
    const std::array<std::uint32_t, 2> ceilingVariantMeshes = {
        buildChippedCeilingVariantMesh(scene, cell, 11003u),
        buildChippedCeilingVariantMesh(scene, cell, 12007u),
    };
    const std::array<std::uint32_t, 2> pillarVariantMeshes = {
        buildChippedPillarMesh(scene, wallHeight, desc.buildTempleTrim, 13001u),
        buildChippedPillarMesh(scene, wallHeight, desc.buildTempleTrim, 14009u),
    };
    const std::array<std::uint32_t, 4> paverMeshes = {
        buildChippedPaverMesh(scene, 0.88f, 0.036f, 0.68f, 15001u),
        buildChippedPaverMesh(scene, 0.72f, 0.032f, 0.74f, 16007u),
        buildChippedPaverMesh(scene, 0.98f, 0.038f, 0.56f, 17011u),
        buildChippedPaverMesh(scene, 0.62f, 0.028f, 0.52f, 18013u),
    };
    const float plankW = std::max(0.06f, desc.plankWidth);
    const float plankT = std::max(0.015f, desc.plankThickness);
    const std::array<std::uint32_t, 3> plankMeshes = {
        buildChippedWoodPlankMesh(scene, 1.0f, plankT, plankW, 19001u),
        buildChippedWoodPlankMesh(scene, 1.0f, plankT, plankW * 0.92f, 20003u),
        buildChippedWoodPlankMesh(scene, 1.0f, plankT, plankW * 1.08f, 21007u),
    };
    const std::array<std::uint32_t, 2> archMeshes = {
        buildSegmentedStoneArchMesh(scene, cell, wallHeight, 5001u),
        buildSegmentedStoneArchMesh(scene, cell, wallHeight, 6007u),
    };
    const std::array<std::uint32_t, 2> plinthMeshes = {
        buildChippedTrimMesh(scene, cell + 0.02f, 0.20f, 0.16f, 4, 7001u),
        buildChippedTrimMesh(scene, cell + 0.02f, 0.20f, 0.16f, 5, 7009u),
    };
    const std::array<std::uint32_t, 2> dadoMeshes = {
        buildChippedTrimMesh(scene, cell + 0.02f, 0.13f, 0.12f, 5, 8001u),
        buildChippedTrimMesh(scene, cell + 0.02f, 0.13f, 0.12f, 4, 8011u),
    };
    const std::array<std::uint32_t, 2> architraveMeshes = {
        buildChippedTrimMesh(scene, cell + 0.02f, 0.15f, 0.14f, 4, 9001u),
        buildChippedTrimMesh(scene, cell + 0.02f, 0.15f, 0.14f, 5, 9013u),
    };

    // Explicit semantic SOR model slots (.dat files placed next to executable)
    LoadedSorModel treasureModel = tryLoadSorModel(scene, exeDir / "treasure.dat", bevelBoxMesh);
    if (!treasureModel.loaded)
    {
        treasureModel = tryLoadSorModel(scene, exeDir / "myModel.dat", bevelBoxMesh);
    }
    const LoadedSorModel candleModel = tryLoadSorModel(scene, exeDir / "candle.dat", bevelBoxMesh);
    const LoadedSorModel torchModel = tryLoadSorModel(scene, exeDir / "torch.dat", bevelBoxMesh);
    const LoadedSorModel vaseModel = tryLoadSorModel(scene, exeDir / "vase.dat", bevelBoxMesh);
    const LoadedSorModel cupModel = tryLoadSorModel(scene, exeDir / "cup.dat", bevelBoxMesh);

    const std::uint32_t worldFlags = kObjectVisibleFlag | kObjectWorldUVFlag;
    const std::uint32_t stoneFlags = worldFlags | kObjectStoneDetailFlag;

    // Continuous dungeon floor base layer (preserved intact for puddle/wetness passes)
    const glm::mat4 floorModel = makeTransform(glm::vec3(totalW * 0.5f, 0.0f, totalH * 0.5f), 0.0f, glm::vec3(totalW, 1.0f, totalH));
    scene.addObject(floorMesh, kMatFloor, floorModel, worldFlags);

    // Chipped 3D masonry walls + clipped stone ceilings (Courtyard cells remain open to moonlight)
    for (int y = 0; y < map.size.y; ++y)
    {
        for (int x = 0; x < map.size.x; ++x)
        {
            const CellKind kind = map.getCell({x, y});
            if (kind == CellKind::Wall)
            {
                if (!hasOpenNeighbor(map, x, y))
                {
                    continue;
                }
                const std::size_t varIdx = static_cast<std::size_t>((x * 73 + y * 137) & 3);
                const float yaw = static_cast<float>((x * 19 + y * 31) & 3) * 1.5707963f;
                const glm::vec3 pos = map.cellCenterWorld({x, y}, 0.0f);
                scene.addObject(wallVariantMeshes[varIdx], kMatStoneWall, makeTransform(pos, yaw, glm::vec3(1.0f)), stoneFlags);
            }
            else if (kind != CellKind::Courtyard)
            {
                const std::size_t ceilIdx = static_cast<std::size_t>((x * 41 + y * 83) & 1);
                const float yaw = static_cast<float>((x * 13 + y * 29) & 3) * 1.5707963f;
                const glm::vec3 roofPos = map.cellCenterWorld({x, y}, wallHeight);
                scene.addObject(ceilingVariantMeshes[ceilIdx], kMatStoneWall, makeTransform(roofPos, yaw, glm::vec3(1.0f)), stoneFlags);
            }
        }
    }

    auto isClearFloorSpot = [&](glm::vec3 cand, float extraPad) -> bool
    {
        for (const glm::vec3 &tp : map.treasurePositions)
        {
            const glm::vec2 d(cand.x - tp.x, cand.z - tp.z);
            const float r = 0.92f + extraPad;
            if (glm::dot(d, d) < r * r)
            {
                return false;
            }
        }
        const glm::vec2 dExit(cand.x - map.exitPos.x, cand.z - map.exitPos.z);
        const float exitR = 1.05f + extraPad;
        return glm::dot(dExit, dExit) >= exitR * exitR;
    };

    // Loose upper-layer chipped flagstones scattered over base floor (leaving gaps for puddles)
    if (desc.buildFloorPavers)
    {
        std::mt19937 paverRng(desc.propSeed ^ 0x918273u);
        std::uniform_real_distribution<float> paverUnit(0.0f, 1.0f);
        const int pMin = std::max(0, desc.paversPerCellMin);
        const int pMax = std::max(pMin, desc.paversPerCellMax);
        std::uniform_int_distribution<int> paverCountDist(pMin, pMax);
        const float maxYawRad = glm::radians(std::max(0.0f, desc.paverMaxYawJitterDeg));
        std::uniform_real_distribution<float> yawJitter(-maxYawRad, maxYawRad);
        const float scaleMin = std::max(0.2f, desc.paverScaleMin);
        const float scaleMax = std::max(scaleMin, desc.paverScaleMax);
        std::uniform_real_distribution<float> scaleRand(scaleMin, scaleMax);
        const float minSepSq = std::max(0.15f, desc.paverMinSeparation) * std::max(0.15f, desc.paverMinSeparation);

        for (int y = 1; y < map.size.y - 1; ++y)
        {
            for (int x = 1; x < map.size.x - 1; ++x)
            {
                if (!map.isWalkable({x, y}))
                {
                    continue;
                }
                const float minX = (map.getCell({x - 1, y}) == CellKind::Wall) ? 0.48f : 0.08f;
                const float maxX = (map.getCell({x + 1, y}) == CellKind::Wall) ? 0.48f : 0.08f;
                const float minZ = (map.getCell({x, y - 1}) == CellKind::Wall) ? 0.48f : 0.08f;
                const float maxZ = (map.getCell({x, y + 1}) == CellKind::Wall) ? 0.48f : 0.08f;

                const glm::vec3 cellMin(static_cast<float>(x) * cell + minX, 0.0f, static_cast<float>(y) * cell + minZ);
                const glm::vec3 cellMax(static_cast<float>(x + 1) * cell - maxX, 0.0f, static_cast<float>(y + 1) * cell - maxZ);
                if (cellMax.x <= cellMin.x || cellMax.z <= cellMin.z)
                {
                    continue;
                }

                const int count = paverCountDist(paverRng);
                std::vector<glm::vec2> placedPavers;
                placedPavers.reserve(static_cast<std::size_t>(count));

                for (int k = 0; k < count; ++k)
                {
                    glm::vec3 cand(0.0f);
                    bool accepted = false;
                    for (int attempt = 0; attempt < 8 && !accepted; ++attempt)
                    {
                        const float px = glm::mix(cellMin.x, cellMax.x, paverUnit(paverRng));
                        const float pz = glm::mix(cellMin.z, cellMax.z, paverUnit(paverRng));
                        cand = glm::vec3(px, 0.0f, pz);
                        if (!isClearFloorSpot(cand, 0.0f))
                        {
                            continue;
                        }
                        bool overlapsPlaced = false;
                        for (const glm::vec2 &prev : placedPavers)
                        {
                            const glm::vec2 diff = prev - glm::vec2(px, pz);
                            if (glm::dot(diff, diff) < minSepSq)
                            {
                                overlapsPlaced = true;
                                break;
                            }
                        }
                        if (!overlapsPlaced)
                        {
                            accepted = true;
                        }
                    }
                    if (!accepted)
                    {
                        continue;
                    }
                    placedPavers.emplace_back(cand.x, cand.z);

                    const float baseYaw = static_cast<float>((k + x + y) & 1) * 1.5707963f;
                    const float yaw = baseYaw + yawJitter(paverRng);
                    const float s = scaleRand(paverRng);
                    const std::size_t meshIdx = static_cast<std::size_t>(k + x * 3 + y * 5) % paverMeshes.size();
                    const std::uint32_t meshId = paverMeshes[meshIdx];
                    scene.addObject(meshId, kMatStoneWall, makeTransform(cand, yaw, glm::vec3(s, 0.85f + 0.35f * paverUnit(paverRng), s)), stoneFlags);

                    constexpr std::array<glm::vec2, 4> kPaverHalfExtents = {{
                        {0.44f, 0.34f},
                        {0.36f, 0.37f},
                        {0.49f, 0.28f},
                        {0.31f, 0.26f},
                    }};
                    floorObstacles.push_back(FloorObstacleObb::fromYaw({cand.x, cand.z}, yaw, kPaverHalfExtents[meshIdx] * s));
                }
            }
        }
    }

    // Scattered weathered timber planks (single or crossed/propped pairs breaking up pure stone)
    if (desc.buildFloorPlanks && desc.plankCellChance > 0.0f)
    {
        std::mt19937 plankRng(desc.propSeed ^ 0x4F2A19u);
        std::uniform_real_distribution<float> plankUnit(0.0f, 1.0f);
        std::uniform_real_distribution<float> plankYaw(0.0f, 6.2831853f);
        const float lenMin = std::max(0.35f, desc.plankLengthMin);
        const float lenMax = std::max(lenMin, desc.plankLengthMax);
        std::uniform_real_distribution<float> lenRand(lenMin, lenMax);
        const float maxTiltRad = glm::radians(std::clamp(desc.plankTiltMaxDeg, 0.0f, 18.0f));
        std::uniform_real_distribution<float> tiltRand(-maxTiltRad, maxTiltRad);
        const int maxPerCluster = std::max(1, desc.planksPerClusterMax);

        for (int y = 1; y < map.size.y - 1; ++y)
        {
            for (int x = 1; x < map.size.x - 1; ++x)
            {
                if (!map.isWalkable({x, y}) || plankUnit(plankRng) > desc.plankCellChance)
                {
                    continue;
                }
                const float minX = (map.getCell({x - 1, y}) == CellKind::Wall) ? 0.65f : 0.20f;
                const float maxX = (map.getCell({x + 1, y}) == CellKind::Wall) ? 0.65f : 0.20f;
                const float minZ = (map.getCell({x, y - 1}) == CellKind::Wall) ? 0.65f : 0.20f;
                const float maxZ = (map.getCell({x, y + 1}) == CellKind::Wall) ? 0.65f : 0.20f;
                if (minX + maxX >= cell || minZ + maxZ >= cell)
                {
                    continue;
                }

                const glm::vec3 anchor(
                    static_cast<float>(x) * cell + glm::mix(minX, cell - maxX, plankUnit(plankRng)),
                    0.0f,
                    static_cast<float>(y) * cell + glm::mix(minZ, cell - maxZ, plankUnit(plankRng)));
                if (!isClearFloorSpot(anchor, 0.35f))
                {
                    continue;
                }

                const int count = 1 + static_cast<int>(plankUnit(plankRng) * static_cast<float>(maxPerCluster));
                for (int p = 0; p < count; ++p)
                {
                    const float len = lenRand(plankRng);
                    const float yaw = plankYaw(plankRng);
                    const bool isTilted = (p > 0) || (plankUnit(plankRng) < 0.35f);
                    const float tiltZ = isTilted ? tiltRand(plankRng) : 0.0f;
                    const float baseH = 0.022f + static_cast<float>(p) * (plankT * 0.85f);
                    const float liftY = baseH + (len * 0.5f) * std::sin(std::abs(tiltZ));

                    const glm::vec3 offset = (p == 0) ? glm::vec3(0.0f) : glm::vec3((plankUnit(plankRng) - 0.5f) * 0.28f, 0.0f, (plankUnit(plankRng) - 0.5f) * 0.28f);
                    const glm::vec3 pos = anchor + offset + glm::vec3(0.0f, liftY, 0.0f);
                    if (!map.isWalkable(map.worldToCell(pos)))
                    {
                        continue;
                    }

                    glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
                    m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
                    if (std::abs(tiltZ) > 1e-4f)
                    {
                        m = glm::rotate(m, tiltZ, glm::vec3(0.0f, 0.0f, 1.0f));
                    }
                    const float plankWidthScale = 0.88f + 0.24f * plankUnit(plankRng);
                    m = glm::scale(m, glm::vec3(len, 1.0f, plankWidthScale));

                    const std::uint32_t meshId = plankMeshes[static_cast<std::size_t>(p + x + y) % plankMeshes.size()];
                    // Omit kObjectWorldUVFlag so wood grain follows each plank's local length axis
                    scene.addObject(meshId, kMatWoodPlank, m, kObjectVisibleFlag);
                    floorObstacles.push_back(FloorObstacleObb::fromYaw({pos.x, pos.z}, yaw, glm::vec2(len * 0.5f, plankW * 0.54f * plankWidthScale)));
                }
            }
        }
    }

    // Stacked chipped masonry corner pillars with 3-tier Y-scaled stone bases & capitals
    for (std::size_t pIdx = 0; pIdx < map.pillars.size(); ++pIdx)
    {
        const glm::vec3 &p = map.pillars[pIdx];
        const float yaw = static_cast<float>(pIdx & 3) * 1.5707963f;
        scene.addObject(pillarVariantMeshes[pIdx % pillarVariantMeshes.size()], kMatStoneWall, makeTransform(p, yaw, glm::vec3(1.0f)), stoneFlags);
        floorObstacles.push_back(FloorObstacleObb::fromYaw({p.x, p.z}, yaw, glm::vec2(0.42f, 0.42f)));
    }

    // Overhead segmented stone archways or corbelled beams along straight corridors
    for (std::size_t bIdx = 0; bIdx < map.ceilingBeams.size(); ++bIdx)
    {
        const PropSpawn &beam = map.ceilingBeams[bIdx];
        if (desc.buildCorridorArches)
        {
            scene.addObject(archMeshes[bIdx % archMeshes.size()], kMatStoneWall, makeTransform(beam.position, beam.yaw, glm::vec3(1.0f)), stoneFlags);
        }
        else
        {
            const glm::vec3 right(std::cos(beam.yaw), 0.0f, -std::sin(beam.yaw));
            scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(beam.position + glm::vec3(0.0f, wallHeight - 0.18f, 0.0f), beam.yaw, glm::vec3(cell * 0.96f, 0.32f, 0.42f)), worldFlags);
            const float bracketOff = cell * 0.41f;
            scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(beam.position + right * bracketOff + glm::vec3(0.0f, wallHeight - 0.34f, 0.0f), beam.yaw, glm::vec3(0.52f, 0.58f, 0.52f)), worldFlags);
            scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(beam.position - right * bracketOff + glm::vec3(0.0f, wallHeight - 0.34f, 0.0f), beam.yaw, glm::vec3(0.52f, 0.58f, 0.52f)), worldFlags);
        }
    }

    // Courtyard ruined cornices, weathered battlements, portal gatehouses & corner finials
    if (desc.buildCourtyardParapets)
    {
        buildCourtyardParapets(map, scene, bevelBoxMesh, rockMeshes, archMeshes, kMatStoneWall, worldFlags, stoneFlags, wallHeight);
    }

    // Segmented chipped stone plinths, dado stringcourses & upper architrave trims
    if (desc.buildTempleTrim)
    {
        buildTempleWallTrim(map, scene, plinthMeshes, dadoMeshes, architraveMeshes, bevelBoxMesh, kMatStoneWall, stoneFlags, wallHeight);
    }

    MazeSceneHandles handles;
    handles.pointLights.reserve(map.wallTorches.size() + map.candles.size() + map.treasurePositions.size() + 1);

    // Wall torches (sconce bracket + turned torch.dat model + animated HDR flame + primary warm point light)
    const float torchScale = std::max(0.1f, desc.torchScale);
    const std::uint32_t torchFlags = worldFlags | render::kObjectNoPointShadowFlag;
    const float torchFitScale = torchModel.loaded ? torchModel.fitScale(0.56f * torchScale, 0.11f * torchScale) : 1.0f;
    for (std::size_t tIdx = 0; tIdx < map.wallTorches.size(); ++tIdx)
    {
        const PropSpawn &torch = map.wallTorches[tIdx];
        const glm::vec3 back = -torch.normal;
        // Sleek wrought-iron wall sconce plate & clasp gripping the torch shaft
        scene.addObject(bevelBoxMesh, kMatTorch, makeTransform(torch.position + back * (0.012f * torchScale) + glm::vec3(0.0f, 0.09f * torchScale, 0.0f), torch.yaw, glm::vec3(0.056f, 0.15f, 0.024f) * torchScale), torchFlags);
        scene.addObject(bevelBoxMesh, kMatTorch, makeTransform(torch.position + torch.normal * (0.018f * torchScale) + glm::vec3(0.0f, 0.09f * torchScale, 0.0f), torch.yaw, glm::vec3(0.068f, 0.036f, 0.072f) * torchScale), torchFlags);

        glm::vec3 flameBasePos = torch.position + glm::vec3(0.0f, 0.34f * torchScale, 0.0f);
        glm::vec3 lightPos = torch.position + torch.normal * (0.14f * torchScale) + glm::vec3(0.0f, 0.48f * torchScale, 0.0f);

        if (torchModel.loaded)
        {
            const glm::vec3 pivotPos = torch.position + torch.normal * (0.042f * torchScale) + glm::vec3(0.0f, 0.14f * torchScale, 0.0f);
            glm::mat4 torchModelMat = glm::translate(glm::mat4(1.0f), pivotPos);
            torchModelMat = glm::rotate(torchModelMat, torch.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            torchModelMat = glm::rotate(torchModelMat, glm::radians(14.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            torchModelMat = glm::scale(torchModelMat, glm::vec3(torchFitScale));
            scene.addObject(torchModel.meshId, kMatTorch, torchModelMat, render::kObjectVisibleFlag | render::kObjectNoPointShadowFlag);

            const float localTopY = torchModel.minY + torchModel.height;
            const glm::vec3 cupTopWorld = glm::vec3(torchModelMat * glm::vec4(0.0f, localTopY, 0.0f, 1.0f));
            flameBasePos = cupTopWorld - glm::vec3(0.0f, 0.025f * torchScale, 0.0f);
            lightPos = cupTopWorld + glm::vec3(0.0f, 0.15f * torchScale, 0.0f);
        }
        else
        {
            scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(torch.position + glm::vec3(0.0f, 0.14f * torchScale, 0.0f), torch.yaw, glm::vec3(0.10f, 0.42f, 0.10f) * torchScale), torchFlags);
        }

        handles.flames.push_back(FlameInstance{
            .posWidth = glm::vec4(flameBasePos, 0.22f * torchScale),
            .heightSeed = glm::vec4(0.36f * torchScale, static_cast<float>(tIdx) * 1.37f, 5.5f, 1.0f),
        });

        handles.pointLights.push_back(GpuPointLight{
            .positionRadius = glm::vec4(lightPos, desc.torchLight.radius),
            .colorIntensity = glm::vec4(desc.torchLight.color, desc.torchLight.intensity),
        });
    }
    handles.torchLightCount = handles.pointLights.size();

    // Clustered candles sharing 1 soft low-intensity light per anchor
    std::mt19937 propRng(desc.propSeed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> angleDist(0.0f, 6.2831853f);

    const int minCandles = std::max(1, desc.candleClusterMinCount);
    const int maxCandles = std::max(minCandles, desc.candleClusterMaxCount);
    std::uniform_int_distribution<int> candleCountDist(minCandles, maxCandles);
    const float clusterRadius = std::max(0.05f, desc.candleClusterRadius);
    const float candleVisualScale = std::max(0.1f, desc.candleScale);
    const float candleBaseScale = candleModel.loaded ? candleModel.fitScale(desc.candleHeight, 0.075f) : 1.0f;

    for (const PropSpawn &clusterAnchor : map.candles)
    {
        const int count = candleCountDist(propRng);
        constexpr float minSepSq = 0.085f * 0.085f;
        std::vector<glm::vec3> placed;
        placed.reserve(static_cast<std::size_t>(count));

        float maxFlameY = 0.22f * candleVisualScale;
        for (int c = 0; c < count; ++c)
        {
            glm::vec3 cPos = clusterAnchor.position;
            bool accepted = (c == 0);
            for (int attempt = 0; attempt < 8 && !accepted; ++attempt)
            {
                const float r = (0.07f + 0.15f * std::sqrt(unit(propRng))) * (clusterRadius / 0.22f);
                const float a = angleDist(propRng);
                const glm::vec3 candidate = clusterAnchor.position + glm::vec3(std::cos(a) * r, 0.0f, std::sin(a) * r);
                if (!map.isWalkable(map.worldToCell(candidate)))
                {
                    continue;
                }
                bool tooClose = false;
                for (const glm::vec3 &prev : placed)
                {
                    const glm::vec2 d(candidate.x - prev.x, candidate.z - prev.z);
                    if (glm::dot(d, d) < minSepSq)
                    {
                        tooClose = true;
                        break;
                    }
                }
                if (!tooClose)
                {
                    cPos = candidate;
                    accepted = true;
                }
            }
            if (!accepted)
            {
                continue;
            }
            placed.push_back(cPos);

            const float hScale = clusterAnchor.scale * candleVisualScale * (0.55f + 0.70f * unit(propRng));
            const float wScale = clusterAnchor.scale * candleVisualScale * (0.80f + 0.35f * unit(propRng));
            const float yaw = angleDist(propRng);

            float flameY = 0.26f * hScale;
            if (candleModel.loaded)
            {
                const float sx = candleBaseScale * wScale;
                const float sy = candleBaseScale * hScale;
                const float posY = -candleModel.minY * sy;
                scene.addObject(candleModel.meshId, kMatStoneWall, makeTransform(cPos + glm::vec3(0.0f, posY, 0.0f), yaw, glm::vec3(sx, sy, sx)));
                flameY = candleModel.height * sy;
            }
            else
            {
                const float h = 0.26f * hScale;
                const float w = 0.12f * wScale;
                scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(cPos + glm::vec3(0.0f, h * 0.5f, 0.0f), yaw, glm::vec3(w, h, w)), worldFlags);
                flameY = h;
            }
            handles.flames.push_back(FlameInstance{
                .posWidth = glm::vec4(cPos + glm::vec3(0.0f, flameY - 0.005f, 0.0f), 0.068f * wScale),
                .heightSeed = glm::vec4(0.12f * hScale, unit(propRng) * 20.0f, 4.2f, 0.0f),
            });
            floorObstacles.push_back(FloorObstacleObb::fromYaw({cPos.x, cPos.z}, yaw, glm::vec2(0.18f, 0.18f)));
            maxFlameY = std::max(maxFlameY, flameY);
        }

        // Single shared dim warm glow (visibly dimmer than wall torches)
        handles.pointLights.push_back(GpuPointLight{
            .positionRadius = glm::vec4(clusterAnchor.position + glm::vec3(0.0f, maxFlameY + 0.12f, 0.0f), desc.candleLight.radius),
            .colorIntensity = glm::vec4(desc.candleLight.color, desc.candleLight.intensity),
        });
    }

    // Floor rubble stones
    for (std::size_t i = 0; i < map.rubbles.size(); ++i)
    {
        const PropSpawn &rock = map.rubbles[i];
        const std::uint32_t meshId = rockMeshes[i % rockMeshes.size()];
        const glm::vec3 s(rock.scale * (0.9f + 0.3f * static_cast<float>(i % 3)), rock.scale, rock.scale * (1.1f - 0.2f * static_cast<float>(i % 2)));
        scene.addObject(meshId, kMatStoneWall, makeTransform(rock.position + glm::vec3(0.0f, rock.scale * 0.32f, 0.0f), rock.yaw, s), worldFlags);
        floorObstacles.push_back(FloorObstacleObb::fromYaw({rock.position.x, rock.position.z}, rock.yaw, glm::vec2(s.x * 0.52f, s.z * 0.52f)));
    }

    // Dead-end treasure pedestals + treasure.dat collectibles (+ decorative cup.dat / vase.dat accents)
    handles.treasureObjectIds.reserve(map.treasurePositions.size());
    handles.treasureBasePositions.reserve(map.treasurePositions.size());
    handles.treasureLightIndices.reserve(map.treasurePositions.size());
    const float treasureScale = treasureModel.loaded ? treasureModel.fitScale(desc.treasureHeight, 0.28f) : 0.42f;
    handles.treasureScale = treasureScale;
    const float cupScale = cupModel.loaded ? cupModel.fitScale(desc.cupHeight, 0.10f) : 0.18f;
    const float vaseScale = vaseModel.loaded ? vaseModel.fitScale(desc.vaseHeight, 0.28f) : 0.45f;

    for (std::size_t idx = 0; idx < map.treasurePositions.size(); ++idx)
    {
        const glm::vec3 &tp = map.treasurePositions[idx];
        scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(tp + glm::vec3(0.0f, 0.14f, 0.0f), 0.0f, glm::vec3(1.05f, 0.28f, 1.05f)), worldFlags);
        scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(tp + glm::vec3(0.0f, 0.56f, 0.0f), 0.0f, glm::vec3(0.68f, 0.60f, 0.68f)), worldFlags);

        const float pedestalTopY = 0.86f;
        if (treasureModel.loaded)
        {
            const float posY = pedestalTopY - treasureModel.minY * treasureScale;
            const glm::vec3 basePos = tp + glm::vec3(0.0f, posY, 0.0f);
            handles.treasureBasePositions.push_back(basePos);
            handles.treasureObjectIds.push_back(scene.addObject(treasureModel.meshId, kMatGoldAccent, makeTransform(basePos, 0.0f, glm::vec3(treasureScale))));
        }
        else
        {
            const glm::vec3 basePos = tp + glm::vec3(0.0f, pedestalTopY + 0.32f, 0.0f);
            handles.treasureBasePositions.push_back(basePos);
            handles.treasureObjectIds.push_back(scene.addObject(bevelBoxMesh, kMatGoldAccent, makeTransform(basePos, 0.78f, glm::vec3(0.42f))));
        }

        handles.treasureLightIndices.push_back(static_cast<std::uint32_t>(handles.pointLights.size()));
        handles.pointLights.push_back(GpuPointLight{
            .positionRadius = glm::vec4(tp + glm::vec3(0.0f, 1.4f, 0.0f), desc.treasureLight.radius),
            .colorIntensity = glm::vec4(desc.treasureLight.color, desc.treasureLight.intensity),
        });
    }

    // Decorative vase.dat & cup.dat props tucked safely against walkable courtyard/corridor walls
    if (desc.enableVasesAndCups && (vaseModel.loaded || cupModel.loaded))
    {
        const float halfCell = cell * 0.5f;
        constexpr glm::ivec2 dirs[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (int y = 1; y < map.size.y - 1; ++y)
        {
            for (int x = 1; x < map.size.x - 1; ++x)
            {
                const glm::ivec2 c{x, y};
                const CellKind kind = map.getCell(c);
                if (kind == CellKind::Wall || c == map.startCell || c == map.exitCell)
                {
                    continue;
                }
                const float chance = (kind == CellKind::Courtyard) ? desc.vaseCourtyardChance : desc.vaseCorridorChance;
                if (unit(propRng) > chance)
                {
                    continue;
                }

                for (const glm::ivec2 d : dirs)
                {
                    if (map.getCell(c + d) != CellKind::Wall)
                    {
                        continue;
                    }
                    const glm::vec3 outward(static_cast<float>(d.x), 0.0f, static_cast<float>(d.y));
                    const glm::vec3 inward = -outward;
                    const glm::vec3 tangent(-inward.z, 0.0f, inward.x);
                    const glm::vec3 wallFace = map.cellCenterWorld(c) + outward * halfCell;
                    const glm::vec3 propPos = wallFace + inward * 0.42f + tangent * ((unit(propRng) - 0.5f) * cell * 0.45f);

                    if (vaseModel.loaded && unit(propRng) < 0.70f)
                    {
                        const float s = vaseScale * (0.85f + 0.30f * unit(propRng));
                        const float posY = -vaseModel.minY * s;
                        scene.addObject(vaseModel.meshId, kMatStoneWall, makeTransform(propPos + glm::vec3(0.0f, posY, 0.0f), angleDist(propRng), glm::vec3(s)));
                        floorObstacles.push_back(FloorObstacleObb::fromYaw({propPos.x, propPos.z}, 0.0f, glm::vec2(0.28f, 0.28f)));

                        if (cupModel.loaded && unit(propRng) < desc.cupWithVaseChance)
                        {
                            const glm::vec3 cupPos = propPos + tangent * 0.34f + inward * 0.08f;
                            if (map.isWalkable(map.worldToCell(cupPos)))
                            {
                                scene.addObject(cupModel.meshId, kMatGoldAccent, makeTransform(cupPos + glm::vec3(0.0f, -cupModel.minY * cupScale, 0.0f), angleDist(propRng), glm::vec3(cupScale)));
                                floorObstacles.push_back(FloorObstacleObb::fromYaw({cupPos.x, cupPos.z}, 0.0f, glm::vec2(0.16f, 0.16f)));
                            }
                        }
                    }
                    else if (cupModel.loaded)
                    {
                        scene.addObject(cupModel.meshId, kMatGoldAccent, makeTransform(propPos + glm::vec3(0.0f, -cupModel.minY * cupScale, 0.0f), angleDist(propRng), glm::vec3(cupScale)));
                        floorObstacles.push_back(FloorObstacleObb::fromYaw({propPos.x, propPos.z}, 0.0f, glm::vec2(0.16f, 0.16f)));
                    }
                    break;
                }
            }
        }
    }

    // Exit shrine marker
    scene.addObject(bevelBoxMesh, kMatGoldAccent, makeTransform(map.exitPos + glm::vec3(0.0f, 0.06f, 0.0f), 0.0f, glm::vec3(1.5f, 0.12f, 1.5f)));
    handles.exitObjectId = scene.addObject(bevelBoxMesh, kMatGoldAccent, makeTransform(map.exitPos + glm::vec3(0.0f, 1.1f, 0.0f), 0.785f, glm::vec3(0.50f, 0.85f, 0.50f)));
    handles.exitLightIndex = static_cast<std::uint32_t>(handles.pointLights.size());
    handles.pointLights.push_back(GpuPointLight{
        .positionRadius = glm::vec4(map.exitPos + glm::vec3(0.0f, 1.6f, 0.0f), desc.exitLight.radius),
        .colorIntensity = glm::vec4(desc.exitLight.color, desc.exitLight.intensity),
    });

    // Stylized courtyard bushes framing open ruins + AABB-guided crawling vines
    setStage(3);
    handles.bushPositions = populateCourtyardBushes(map, scene, kMatBushFoliage, floorObstacles, desc.foliage);

    VineGenDesc vineDesc = desc.vines;
    vineDesc.wallHeight = wallHeight;
    generateDungeonVines(map, scene, kMatVineMoss, vineDesc);

    if (grassInstancesOut != nullptr)
    {
        setStage(4);
        *grassInstancesOut = generateMazeGrass(map, floorObstacles, desc.foliage);
    }
    return handles;
}

} // namespace

BuiltMazeSceneCpu buildMazeSceneCpu(const MazeGenDesc &mapDesc, const MazeSceneDesc &sceneDesc, const std::filesystem::path &exeDir, std::atomic<int> *stageOut)
{
    if (stageOut != nullptr)
    {
        stageOut->store(1, std::memory_order_relaxed);
    }
    BuiltMazeSceneCpu result;
    result.map = generateMaze(mapDesc);
    result.wallHeight = sceneDesc.wallHeight;
    result.handles = populateMazeSceneCpu(result.map, result.scene, exeDir, sceneDesc, &result.grassInstances, stageOut);
    return result;
}

MazeSceneHandles buildMazeScene(const MazeMapData &map, render::MegaScene &scene, const std::filesystem::path &exeDir, const MazeSceneDesc &desc, render::GrassField *grassOut)
{
    std::vector<render::GrassInstance> grassInstances;
    MazeSceneHandles handles = populateMazeSceneCpu(map, scene, exeDir, desc, (grassOut != nullptr) ? &grassInstances : nullptr, nullptr);
    scene.buildGpuBuffers();
    if (grassOut != nullptr)
    {
        grassOut->uploadInstances(grassInstances);
    }
    return handles;
}

} // namespace game
