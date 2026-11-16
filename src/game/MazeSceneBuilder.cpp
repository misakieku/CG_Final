#include "game/MazeSceneBuilder.hpp"
#include "game/VineGenerator.hpp"

#include "common/dat_mesh.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace game
{

namespace
{

constexpr std::uint32_t kMatFloor = 0;
constexpr std::uint32_t kMatStoneWall = 1;
constexpr std::uint32_t kMatGoldAccent = 2;
constexpr std::uint32_t kMatVineMoss = 3;

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

void computeBoundsY(const cg::DatMeshData &mesh, float &outMinY, float &outMaxY) noexcept
{
    if (mesh.positions.empty())
    {
        outMinY = 0.0f;
        outMaxY = 1.0f;
        return;
    }
    outMinY = outMaxY = mesh.positions[0].y;
    for (const glm::vec3 &p : mesh.positions)
    {
        outMinY = std::min(outMinY, p.y);
        outMaxY = std::max(outMaxY, p.y);
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

    bool hasCustomSor = false;
    std::uint32_t sorMesh = boxMesh;
    float sorMinY = -0.5f;
    float sorScale = 0.55f;
    const std::filesystem::path datPath = exeDir / "myModel.dat";
    if (std::filesystem::exists(datPath))
    {
        const cg::DatMeshData dat = cg::loadDatMesh(datPath);
        if (!dat.positions.empty() && !dat.indices.empty())
        {
            float maxY = 1.0f;
            computeBoundsY(dat, sorMinY, maxY);
            const float h = std::max(0.05f, maxY - sorMinY);
            sorScale = 0.85f / h;
            sorMesh = scene.registerDatMesh(dat);
            hasCustomSor = true;
        }
    }

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

    // 5. Wall torches (bracket + shaft + glowing ember tip blockout)
    for (const PropSpawn &torch : map.wallTorches)
    {
        const glm::vec3 back = -torch.normal;
        scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(torch.position + back * 0.08f, torch.yaw, glm::vec3(0.16f, 0.24f, 0.16f)), worldFlags);
        scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(torch.position + glm::vec3(0.0f, 0.14f, 0.0f), torch.yaw, glm::vec3(0.10f, 0.42f, 0.10f)), worldFlags);
        scene.addObject(bevelBoxMesh, kMatGoldAccent, makeTransform(torch.position + glm::vec3(0.0f, 0.39f, 0.0f), torch.yaw, glm::vec3(0.13f, 0.14f, 0.13f)));
    }

    // 6. Square candle pillars + tiny ember tip
    for (const PropSpawn &candle : map.candles)
    {
        const float h = 0.26f * candle.scale;
        const float w = 0.13f * candle.scale;
        scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(candle.position + glm::vec3(0.0f, h * 0.5f, 0.0f), candle.yaw, glm::vec3(w, h, w)), worldFlags);
        scene.addObject(bevelBoxMesh, kMatGoldAccent, makeTransform(candle.position + glm::vec3(0.0f, h + 0.04f, 0.0f), candle.yaw, glm::vec3(w * 0.45f, 0.09f, w * 0.45f)));
    }

    // 7. Floor rubble stones
    for (std::size_t i = 0; i < map.rubbles.size(); ++i)
    {
        const PropSpawn &rock = map.rubbles[i];
        const std::uint32_t meshId = rockMeshes[i % rockMeshes.size()];
        const glm::vec3 s(rock.scale * (0.9f + 0.3f * static_cast<float>(i % 3)), rock.scale, rock.scale * (1.1f - 0.2f * static_cast<float>(i % 2)));
        scene.addObject(meshId, kMatStoneWall, makeTransform(rock.position + glm::vec3(0.0f, rock.scale * 0.32f, 0.0f), rock.yaw, s), worldFlags);
    }

    // 8. Dead-end treasure pedestals + SOR collectibles
    MazeSceneHandles handles;
    handles.treasureObjectIds.reserve(map.treasurePositions.size());
    for (const glm::vec3 &tp : map.treasurePositions)
    {
        scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(tp + glm::vec3(0.0f, 0.14f, 0.0f), 0.0f, glm::vec3(1.05f, 0.28f, 1.05f)), worldFlags);
        scene.addObject(bevelBoxMesh, kMatStoneWall, makeTransform(tp + glm::vec3(0.0f, 0.56f, 0.0f), 0.0f, glm::vec3(0.68f, 0.60f, 0.68f)), worldFlags);

        const float pedestalTopY = 0.86f;
        if (hasCustomSor)
        {
            const float posY = pedestalTopY - sorMinY * sorScale + 0.06f;
            handles.treasureObjectIds.push_back(scene.addObject(sorMesh, kMatGoldAccent, makeTransform(tp + glm::vec3(0.0f, posY, 0.0f), 0.0f, glm::vec3(sorScale))));
        }
        else
        {
            handles.treasureObjectIds.push_back(scene.addObject(bevelBoxMesh, kMatGoldAccent, makeTransform(tp + glm::vec3(0.0f, pedestalTopY + 0.32f, 0.0f), 0.78f, glm::vec3(0.42f))));
        }
    }

    // 9. Exit shrine marker
    scene.addObject(bevelBoxMesh, kMatGoldAccent, makeTransform(map.exitPos + glm::vec3(0.0f, 0.06f, 0.0f), 0.0f, glm::vec3(1.5f, 0.12f, 1.5f)));
    handles.exitObjectId = scene.addObject(bevelBoxMesh, kMatGoldAccent, makeTransform(map.exitPos + glm::vec3(0.0f, 1.1f, 0.0f), 0.785f, glm::vec3(0.50f, 0.85f, 0.50f)));

    // 10. AABB-guided crawling & hanging dungeon vines
    generateDungeonVines(map, scene, kMatVineMoss, wallHeight);

    scene.buildGpuBuffers();
    return handles;
}

} // namespace game
