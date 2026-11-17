#include "game/MazeSceneBuilder.hpp"
#include "game/OasisGenerator.hpp"
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
constexpr std::uint32_t kMatOasisSand = 4;
constexpr std::uint32_t kMatOasisWater = 5;
constexpr std::uint32_t kMatPalmBark = 6;
constexpr std::uint32_t kMatTropicalFoliage = 7;

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

void buildCourtyardParapets(const MazeMapData &map, render::MegaScene &scene, std::uint32_t bevelBoxMesh, std::span<const std::uint32_t> rockMeshes, std::uint32_t stoneMat, std::uint32_t worldFlags, float wallHeight)
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

} // namespace

MazeSceneHandles buildMazeScene(const MazeMapData &map, render::MegaScene &scene, const std::filesystem::path &exeDir, float wallHeight)
{
    using namespace render;

    const float totalW = static_cast<float>(map.size.x) * map.cellSize;
    const float totalH = static_cast<float>(map.size.y) * map.cellSize;

    const std::uint32_t floorMesh = registerPlaneMesh(scene, 0.5f, 1.0f);
    const std::uint32_t boxMesh = registerUnitBoxMesh(scene, 0.5f, 1.0f);
    const std::uint32_t bevelBoxMesh = registerBeveledBoxMesh(scene, 0.5f, 0.045f);
    const std::array<std::uint32_t, 3> rockMeshes = {
        registerRockMesh(scene, 101u),
        registerRockMesh(scene, 202u),
        registerRockMesh(scene, 303u),
    };

    // Explicit semantic SOR model slots (.dat files placed next to executable)
    LoadedSorModel treasureModel = tryLoadSorModel(scene, exeDir / "treasure.dat", bevelBoxMesh);
    if (!treasureModel.loaded)
    {
        treasureModel = tryLoadSorModel(scene, exeDir / "myModel.dat", bevelBoxMesh);
    }
    const LoadedSorModel candleModel = tryLoadSorModel(scene, exeDir / "candle.dat", bevelBoxMesh);
    const LoadedSorModel vaseModel = tryLoadSorModel(scene, exeDir / "vase.dat", bevelBoxMesh);
    const LoadedSorModel cupModel = tryLoadSorModel(scene, exeDir / "cup.dat", bevelBoxMesh);

    const std::uint32_t worldFlags = kObjectVisibleFlag | kObjectWorldUVFlag;

    // 1. Continuous dungeon floor covering the maze footprint
    const glm::mat4 floorModel = makeTransform(glm::vec3(totalW * 0.5f, 0.0f, totalH * 0.5f), 0.0f, glm::vec3(totalW, 1.0f, totalH));
    scene.addObject(floorMesh, kMatFloor, floorModel, worldFlags);

    // 2. Visible wall blocks + roofed corridors (Courtyard cells remain open to moonlight)
    const float cell = map.cellSize;
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
                const glm::vec3 pos = map.cellCenterWorld({x, y}, wallHeight * 0.5f);
                scene.addObject(boxMesh, kMatStoneWall, makeTransform(pos, 0.0f, glm::vec3(cell, wallHeight, cell)), worldFlags);
            }
            else if (kind != CellKind::Courtyard)
            {
                const glm::vec3 roofPos = map.cellCenterWorld({x, y}, wallHeight + 0.15f);
                scene.addObject(boxMesh, kMatStoneWall, makeTransform(roofPos, 0.0f, glm::vec3(cell, 0.30f, cell)), worldFlags);
            }
        }
    }

    // 3. Stepped square stone pillars at corners (base plinth + shaft + top cap)
    for (const glm::vec3 &p : map.pillars)
    {
        scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(p + glm::vec3(0.0f, 0.14f, 0.0f), 0.0f, glm::vec3(0.76f, 0.28f, 0.76f)), worldFlags);
        scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(p + glm::vec3(0.0f, wallHeight * 0.5f, 0.0f), 0.0f, glm::vec3(0.54f, wallHeight + 0.06f, 0.54f)), worldFlags);
        scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(p + glm::vec3(0.0f, wallHeight - 0.12f, 0.0f), 0.0f, glm::vec3(0.78f, 0.28f, 0.78f)), worldFlags);
    }

    // 4. Overhead corbelled stone ceiling beams
    for (const PropSpawn &beam : map.ceilingBeams)
    {
        const glm::vec3 right(std::cos(beam.yaw), 0.0f, -std::sin(beam.yaw));
        scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(beam.position + glm::vec3(0.0f, wallHeight - 0.18f, 0.0f), beam.yaw, glm::vec3(cell * 0.96f, 0.32f, 0.42f)), worldFlags);
        const float bracketOff = cell * 0.41f;
        scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(beam.position + right * bracketOff + glm::vec3(0.0f, wallHeight - 0.34f, 0.0f), beam.yaw, glm::vec3(0.52f, 0.58f, 0.52f)), worldFlags);
        scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(beam.position - right * bracketOff + glm::vec3(0.0f, wallHeight - 0.34f, 0.0f), beam.yaw, glm::vec3(0.52f, 0.58f, 0.52f)), worldFlags);
    }

    // 4b. Courtyard ruined cornices, weathered battlements & corner finials
    buildCourtyardParapets(map, scene, bevelBoxMesh, rockMeshes, kMatStoneWall, worldFlags, wallHeight);

    MazeSceneHandles handles;
    handles.pointLights.reserve(map.wallTorches.size() + map.candles.size() + map.treasurePositions.size() + 1);

    // 5. Wall torches (bracket + shaft + glowing ember tip + primary warm point light)
    for (const PropSpawn &torch : map.wallTorches)
    {
        const glm::vec3 back = -torch.normal;
        scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(torch.position + back * 0.08f, torch.yaw, glm::vec3(0.16f, 0.24f, 0.16f)), worldFlags);
        scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(torch.position + glm::vec3(0.0f, 0.14f, 0.0f), torch.yaw, glm::vec3(0.10f, 0.42f, 0.10f)), worldFlags);
        scene.addObject(bevelBoxMesh, kMatGoldAccent, makeTransform(torch.position + glm::vec3(0.0f, 0.39f, 0.0f), torch.yaw, glm::vec3(0.13f, 0.14f, 0.13f)));

        const glm::vec3 lightPos = torch.position + torch.normal * 0.14f + glm::vec3(0.0f, 0.48f, 0.0f);
        handles.pointLights.push_back(GpuPointLight{
            .positionRadius = glm::vec4(lightPos, 9.2f),
            .colorIntensity = glm::vec4(1.0f, 0.52f, 0.18f, 5.5f),
        });
    }

    // 6. Clustered candles: 2-4 varied candles within a small radius sharing 1 soft low-intensity light
    std::mt19937 propRng(424242u);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> angleDist(0.0f, 6.2831853f);

    const float candleBaseScale = candleModel.loaded ? candleModel.fitScale(0.28f, 0.075f) : 1.0f;
    for (const PropSpawn &clusterAnchor : map.candles)
    {
        const int count = 2 + static_cast<int>(unit(propRng) * 3.0f); // 2..4 candles per cluster
        constexpr float clusterRadius = 0.22f;
        constexpr float minSepSq = 0.085f * 0.085f;
        std::vector<glm::vec3> placed;
        placed.reserve(static_cast<std::size_t>(count));

        float maxFlameY = 0.22f;
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

            const float hScale = clusterAnchor.scale * (0.55f + 0.70f * unit(propRng));
            const float wScale = clusterAnchor.scale * (0.80f + 0.35f * unit(propRng));
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
            scene.addObject(bevelBoxMesh, kMatGoldAccent, makeTransform(cPos + glm::vec3(0.0f, flameY + 0.03f, 0.0f), yaw, glm::vec3(0.045f, 0.07f, 0.045f)));
            maxFlameY = std::max(maxFlameY, flameY);
        }

        // Single shared dim warm glow (visibly dimmer than wall torches)
        handles.pointLights.push_back(GpuPointLight{
            .positionRadius = glm::vec4(clusterAnchor.position + glm::vec3(0.0f, maxFlameY + 0.12f, 0.0f), 3.3f),
            .colorIntensity = glm::vec4(1.0f, 0.62f, 0.24f, 0.78f),
        });
    }

    // 7. Floor rubble stones
    for (std::size_t i = 0; i < map.rubbles.size(); ++i)
    {
        const PropSpawn &rock = map.rubbles[i];
        const std::uint32_t meshId = rockMeshes[i % rockMeshes.size()];
        const glm::vec3 s(rock.scale * (0.9f + 0.3f * static_cast<float>(i % 3)), rock.scale, rock.scale * (1.1f - 0.2f * static_cast<float>(i % 2)));
        scene.addObject(meshId, kMatStoneWall, makeTransform(rock.position + glm::vec3(0.0f, rock.scale * 0.32f, 0.0f), rock.yaw, s), worldFlags);
    }

    // 8. Dead-end treasure pedestals + treasure.dat collectibles (+ decorative cup.dat / vase.dat accents)
    handles.treasureObjectIds.reserve(map.treasurePositions.size());
    handles.treasureBasePositions.reserve(map.treasurePositions.size());
    handles.treasureLightIndices.reserve(map.treasurePositions.size());
    const float treasureScale = treasureModel.loaded ? treasureModel.fitScale(0.85f, 0.28f) : 0.42f;
    handles.treasureScale = treasureScale;
    const float cupScale = cupModel.loaded ? cupModel.fitScale(0.28f, 0.10f) : 0.18f;
    const float vaseScale = vaseModel.loaded ? vaseModel.fitScale(0.74f, 0.28f) : 0.45f;

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
            .positionRadius = glm::vec4(tp + glm::vec3(0.0f, 1.4f, 0.0f), 4.2f),
            .colorIntensity = glm::vec4(1.0f, 0.80f, 0.35f, 1.25f),
        });
    }

    // 8b. Decorative vase.dat & cup.dat props tucked safely against walkable courtyard/corridor walls
    if (vaseModel.loaded || cupModel.loaded)
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
                const float chance = (kind == CellKind::Courtyard) ? 0.28f : 0.09f;
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

                        if (cupModel.loaded && unit(propRng) < 0.45f)
                        {
                            const glm::vec3 cupPos = propPos + tangent * 0.34f + inward * 0.08f;
                            if (map.isWalkable(map.worldToCell(cupPos)))
                            {
                                scene.addObject(cupModel.meshId, kMatGoldAccent, makeTransform(cupPos + glm::vec3(0.0f, -cupModel.minY * cupScale, 0.0f), angleDist(propRng), glm::vec3(cupScale)));
                            }
                        }
                    }
                    else if (cupModel.loaded)
                    {
                        scene.addObject(cupModel.meshId, kMatGoldAccent, makeTransform(propPos + glm::vec3(0.0f, -cupModel.minY * cupScale, 0.0f), angleDist(propRng), glm::vec3(cupScale)));
                    }
                    break;
                }
            }
        }
    }

    // 9. Exit shrine marker
    scene.addObject(bevelBoxMesh, kMatGoldAccent, makeTransform(map.exitPos + glm::vec3(0.0f, 0.06f, 0.0f), 0.0f, glm::vec3(1.5f, 0.12f, 1.5f)));
    handles.exitObjectId = scene.addObject(bevelBoxMesh, kMatGoldAccent, makeTransform(map.exitPos + glm::vec3(0.0f, 1.1f, 0.0f), 0.785f, glm::vec3(0.50f, 0.85f, 0.50f)));
    handles.exitLightIndex = static_cast<std::uint32_t>(handles.pointLights.size());
    handles.pointLights.push_back(GpuPointLight{
        .positionRadius = glm::vec4(map.exitPos + glm::vec3(0.0f, 1.6f, 0.0f), 7.5f),
        .colorIntensity = glm::vec4(0.25f, 0.95f, 0.65f, 3.8f),
    });

    // 10. Procedural desert oases inside >= 5x5 open-sky courtyards
    const OasisMaterialIds oasisMaterials{
        .stoneWall = kMatStoneWall,
        .warmEmber = kMatGoldAccent,
        .sandBank = kMatOasisSand,
        .waterSurface = kMatOasisWater,
        .palmBark = kMatPalmBark,
        .tropicalFoliage = kMatTropicalFoliage,
    };
    buildMazeOases(map, scene, rockMeshes, bevelBoxMesh, oasisMaterials, handles.pointLights);

    // 11. AABB-guided crawling & hanging dungeon vines
    generateDungeonVines(map, scene, kMatVineMoss, wallHeight);

    scene.buildGpuBuffers();
    return handles;
}

} // namespace game
