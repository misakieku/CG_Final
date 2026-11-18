#include "game/GameSession.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace game
{

namespace
{

constexpr float kEyeHeight = 1.68f;
constexpr float kPlayerRadius = 0.36f;
constexpr float kWalkSpeed = 5.4f;
constexpr float kSprintSpeed = 8.4f;
constexpr float kFlySpeed = 9.5f;
constexpr float kGravity = 16.0f;
constexpr float kJumpSpeed = 5.2f;
constexpr float kPickupRadius = 1.45f;

[[nodiscard]] float findInitialLookYaw(const MazeMapData &map) noexcept
{
    constexpr glm::ivec2 dirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const glm::ivec2 d : dirs)
    {
        if (map.isWalkable(map.startCell + d))
        {
            return std::atan2(static_cast<float>(d.y), static_cast<float>(d.x));
        }
    }
    return 0.0f;
}

void pushCircleOutOfAabb(glm::vec2 &pos, float radius, glm::vec2 boxMin, glm::vec2 boxMax) noexcept
{
    const glm::vec2 closest = glm::clamp(pos, boxMin, boxMax);
    const glm::vec2 delta = pos - closest;
    const float distSq = glm::dot(delta, delta);
    if (distSq >= radius * radius)
    {
        return;
    }

    if (distSq > 1e-8f)
    {
        const float dist = std::sqrt(distSq);
        pos += (delta / dist) * (radius - dist);
        return;
    }

    // Center is inside the box: push out along nearest face
    const float leftPen = pos.x - boxMin.x;
    const float rightPen = boxMax.x - pos.x;
    const float topPen = pos.y - boxMin.y;
    const float botPen = boxMax.y - pos.y;
    const float minPen = std::min({leftPen, rightPen, topPen, botPen});
    if (minPen == leftPen)
    {
        pos.x = boxMin.x - radius;
    }
    else if (minPen == rightPen)
    {
        pos.x = boxMax.x + radius;
    }
    else if (minPen == topPen)
    {
        pos.y = boxMin.y - radius;
    }
    else
    {
        pos.y = boxMax.y + radius;
    }
}

void pushCircleOutOfOasis(glm::vec2 &pos, float radius, const OasisSpawn &oasis) noexcept
{
    const float c = std::cos(-oasis.yaw);
    const float s = std::sin(-oasis.yaw);
    const glm::vec2 rel = pos - glm::vec2(oasis.center.x, oasis.center.z);
    const glm::vec2 local(rel.x * c - rel.y * s, rel.x * s + rel.y * c);

    const float rx = oasis.radiusX * 0.90f + radius;
    const float rz = oasis.radiusZ * 0.90f + radius;
    const float nx = local.x / rx;
    const float nz = local.y / rz;
    const float ellSq = nx * nx + nz * nz;
    if (ellSq >= 1.0f || ellSq <= 1e-8f)
    {
        return;
    }

    const float invLen = 1.0f / std::sqrt(ellSq);
    const glm::vec2 pushedLocal(local.x * invLen, local.y * invLen);
    const float rc = std::cos(oasis.yaw);
    const float rs = std::sin(oasis.yaw);
    pos = glm::vec2(oasis.center.x, oasis.center.z) + glm::vec2(pushedLocal.x * rc - pushedLocal.y * rs, pushedLocal.x * rs + pushedLocal.y * rc);
}

[[nodiscard]] bool hasLineOfSight(const MazeMapData &map, glm::ivec2 a, glm::ivec2 b) noexcept
{
    const int dx = b.x - a.x;
    const int dy = b.y - a.y;
    const int steps = std::max(std::abs(dx), std::abs(dy)) * 2;
    if (steps <= 1)
    {
        return true;
    }

    const glm::vec2 start(static_cast<float>(a.x) + 0.5f, static_cast<float>(a.y) + 0.5f);
    const glm::vec2 end(static_cast<float>(b.x) + 0.5f, static_cast<float>(b.y) + 0.5f);
    for (int i = 1; i < steps; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const glm::vec2 p = glm::mix(start, end, t);
        const glm::ivec2 cell(static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)));
        if (!map.isWalkable(cell))
        {
            return false;
        }
    }
    return true;
}

} // namespace

void GameSession::reset(const MazeMapData &map, const MazeSceneHandles &handles)
{
    m_map = &map;
    m_handles = handles;

    m_position = glm::vec3(map.playerStart.x, 0.0f, map.playerStart.z);
    m_verticalVel = 0.0f;
    m_yaw = findInitialLookYaw(map);
    m_pitch = -0.05f;
    m_bobPhase = 0.0f;
    m_bobOffset = 0.0f;
    m_grounded = true;

    m_collected.assign(handles.treasureObjectIds.size(), 0);
    m_explored.assign(static_cast<std::size_t>(std::max(0, map.size.x * map.size.y)), 0);
    m_collectedCount = 0;
    m_won = false;
    m_runTimer = 0.0f;
    m_lockedHintTimer = 0.0f;

    updateExploration();
}

glm::vec3 GameSession::forwardDirection() const noexcept
{
    const float cp = std::cos(m_pitch);
    return glm::normalize(glm::vec3(cp * std::cos(m_yaw), std::sin(m_pitch), cp * std::sin(m_yaw)));
}

glm::vec3 GameSession::eyePosition() const noexcept
{
    if (m_flyMode)
    {
        return m_position;
    }
    return m_position + glm::vec3(0.0f, kEyeHeight + m_bobOffset, 0.0f);
}

glm::mat4 GameSession::viewMatrix() const noexcept
{
    const glm::vec3 eye = eyePosition();
    return glm::lookAt(eye, eye + forwardDirection(), glm::vec3(0.0f, 1.0f, 0.0f));
}

void GameSession::update(const Window &window, float dt, render::RenderResources &resources)
{
    if (m_map == nullptr)
    {
        return;
    }

    if (window.wasKeyPressed(GLFW_KEY_F))
    {
        if (!m_flyMode)
        {
            m_position.y += kEyeHeight;
            m_flyMode = true;
        }
        else
        {
            m_position.y = 0.0f;
            m_verticalVel = 0.0f;
            m_grounded = true;
            m_flyMode = false;
            glm::vec2 xz(m_position.x, m_position.z);
            resolveCollisions(xz);
            m_position.x = xz.x;
            m_position.z = xz.y;
        }
    }
    if (window.wasKeyPressed(GLFW_KEY_M))
    {
        m_minimapExpanded = !m_minimapExpanded;
    }
    if (window.wasKeyPressed(GLFW_KEY_TAB))
    {
        m_revealFullMap = !m_revealFullMap;
    }

    if (!m_won)
    {
        m_runTimer += dt;
    }
    if (m_lockedHintTimer > 0.0f)
    {
        m_lockedHintTimer = std::max(0.0f, m_lockedHintTimer - dt);
    }

    updateMovement(window, dt);
    updateExploration();
    updateWorldInteractions(dt, window.elapsedTime(), resources);
}

void GameSession::updateMovement(const Window &window, float dt)
{
    constexpr float kLookSensitivity = 0.0022f;
    if (window.isCursorCaptured())
    {
        const glm::vec2 delta = window.mouseDelta();
        m_yaw += delta.x * kLookSensitivity;
        m_pitch = std::clamp(m_pitch - delta.y * kLookSensitivity, -1.48f, 1.48f);
    }

    if (m_flyMode)
    {
        const glm::vec3 fwd = forwardDirection();
        const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 move(0.0f);
        if (window.isKeyDown(GLFW_KEY_W)) { move += fwd; }
        if (window.isKeyDown(GLFW_KEY_S)) { move -= fwd; }
        if (window.isKeyDown(GLFW_KEY_D)) { move += right; }
        if (window.isKeyDown(GLFW_KEY_A)) { move -= right; }
        if (window.isKeyDown(GLFW_KEY_SPACE)) { move.y += 1.0f; }
        if (window.isKeyDown(GLFW_KEY_LEFT_SHIFT)) { move.y -= 1.0f; }

        if (glm::dot(move, move) > 1e-6f)
        {
            m_position += glm::normalize(move) * (kFlySpeed * dt);
        }
        return;
    }

    // Grounded FPS movement with sliding collision
    const glm::vec3 flatFwd(std::cos(m_yaw), 0.0f, std::sin(m_yaw));
    const glm::vec3 flatRight(-flatFwd.z, 0.0f, flatFwd.x);
    glm::vec2 moveXZ(0.0f);
    if (window.isKeyDown(GLFW_KEY_W)) { moveXZ += glm::vec2(flatFwd.x, flatFwd.z); }
    if (window.isKeyDown(GLFW_KEY_S)) { moveXZ -= glm::vec2(flatFwd.x, flatFwd.z); }
    if (window.isKeyDown(GLFW_KEY_D)) { moveXZ += glm::vec2(flatRight.x, flatRight.z); }
    if (window.isKeyDown(GLFW_KEY_A)) { moveXZ -= glm::vec2(flatRight.x, flatRight.z); }

    const float speed = window.isKeyDown(GLFW_KEY_LEFT_SHIFT) ? kSprintSpeed : kWalkSpeed;
    const bool moving = glm::dot(moveXZ, moveXZ) > 1e-6f;
    if (moving)
    {
        const glm::vec2 step = glm::normalize(moveXZ) * (speed * dt);
        glm::vec2 xz(m_position.x + step.x, m_position.z + step.y);
        resolveCollisions(xz);
        resolveCollisions(xz);
        m_position.x = xz.x;
        m_position.z = xz.y;

        if (m_grounded)
        {
            m_bobPhase += dt * (window.isKeyDown(GLFW_KEY_LEFT_SHIFT) ? 13.0f : 9.5f);
            m_bobOffset = std::sin(m_bobPhase) * 0.026f;
        }
    }
    else
    {
        m_bobOffset *= std::max(0.0f, 1.0f - dt * 10.0f);
    }

    if (m_grounded && window.wasKeyPressed(GLFW_KEY_SPACE))
    {
        m_verticalVel = kJumpSpeed;
        m_grounded = false;
    }

    if (!m_grounded)
    {
        m_verticalVel -= kGravity * dt;
        m_position.y += m_verticalVel * dt;
        if (m_position.y <= 0.0f)
        {
            m_position.y = 0.0f;
            m_verticalVel = 0.0f;
            m_grounded = true;
        }
    }
}

void GameSession::resolveCollisions(glm::vec2 &xz) const noexcept
{
    const float cell = m_map->cellSize;
    const glm::ivec2 centerCell = m_map->worldToCell(glm::vec3(xz.x, 0.0f, xz.y));

    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            const glm::ivec2 c = centerCell + glm::ivec2(dx, dy);
            if (!m_map->inBounds(c) || m_map->getCell(c) == CellKind::Wall)
            {
                const glm::vec2 bMin(static_cast<float>(c.x) * cell, static_cast<float>(c.y) * cell);
                const glm::vec2 bMax = bMin + glm::vec2(cell);
                pushCircleOutOfAabb(xz, kPlayerRadius, bMin, bMax);
            }
        }
    }

    for (const glm::vec3 &pillar : m_map->pillars)
    {
        const glm::vec2 p(pillar.x, pillar.z);
        pushCircleOutOfAabb(xz, kPlayerRadius, p - glm::vec2(0.38f), p + glm::vec2(0.38f));
    }

    for (const glm::vec3 &tp : m_map->treasurePositions)
    {
        const glm::vec2 p(tp.x, tp.z);
        pushCircleOutOfAabb(xz, kPlayerRadius, p - glm::vec2(0.52f), p + glm::vec2(0.52f));
    }

    for (const OasisSpawn &oasis : m_map->oases)
    {
        pushCircleOutOfOasis(xz, kPlayerRadius, oasis);
    }
}

void GameSession::updateExploration() noexcept
{
    const glm::ivec2 pCell = m_map->worldToCell(m_position);
    constexpr int kVisionRadius = 4;

    for (int dy = -kVisionRadius; dy <= kVisionRadius; ++dy)
    {
        for (int dx = -kVisionRadius; dx <= kVisionRadius; ++dx)
        {
            if (dx * dx + dy * dy > kVisionRadius * kVisionRadius + 2)
            {
                continue;
            }
            const glm::ivec2 target = pCell + glm::ivec2(dx, dy);
            if (!m_map->inBounds(target) || !m_map->isWalkable(target))
            {
                continue;
            }
            if (!hasLineOfSight(*m_map, pCell, target))
            {
                continue;
            }

            m_explored[static_cast<std::size_t>(m_map->getCellIndex(target.x, target.y))] = 1;

            // Reveal adjacent solid wall cells so corridors frame cleanly on the minimap
            for (int wy = -1; wy <= 1; ++wy)
            {
                for (int wx = -1; wx <= 1; ++wx)
                {
                    const glm::ivec2 w = target + glm::ivec2(wx, wy);
                    if (m_map->inBounds(w))
                    {
                        m_explored[static_cast<std::size_t>(m_map->getCellIndex(w.x, w.y))] = 1;
                    }
                }
            }
        }
    }
}

void GameSession::updateWorldInteractions(float dt, float elapsedTime, render::RenderResources &resources)
{
    (void)dt;
    render::MegaScene &scene = resources.scene();
    const glm::vec2 playerXZ(m_position.x, m_position.z);

    // Rotate uncollected treasures & detect pickup
    for (std::size_t i = 0; i < m_handles.treasureObjectIds.size(); ++i)
    {
        if (m_collected[i] != 0)
        {
            continue;
        }

        if (i < m_handles.treasureBasePositions.size())
        {
            const glm::vec3 basePos = m_handles.treasureBasePositions[i];
            const float bob = std::sin(elapsedTime * 2.4f + static_cast<float>(i) * 1.7f) * 0.055f;
            glm::mat4 model = glm::translate(glm::mat4(1.0f), basePos + glm::vec3(0.0f, bob, 0.0f));
            model = glm::rotate(model, elapsedTime * 1.35f + static_cast<float>(i), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::scale(model, glm::vec3(m_handles.treasureScale));
            scene.updateObjectTransform(m_handles.treasureObjectIds[i], model);
        }

        const glm::vec2 tpXZ(m_map->treasurePositions[i].x, m_map->treasurePositions[i].z);
        const glm::vec2 d = playerXZ - tpXZ;
        if (glm::dot(d, d) <= kPickupRadius * kPickupRadius && !m_flyMode)
        {
            m_collected[i] = 1;
            ++m_collectedCount;
            scene.setObjectVisible(m_handles.treasureObjectIds[i], false);

            if (i < m_handles.treasureLightIndices.size())
            {
                const std::uint32_t lightIdx = m_handles.treasureLightIndices[i];
                if (lightIdx < m_handles.pointLights.size())
                {
                    m_handles.pointLights[lightIdx].colorIntensity.w = 0.20f;
                    resources.updatePointLight(lightIdx, m_handles.pointLights[lightIdx]);
                }
            }
        }
    }

    // Spin exit crystal & handle exit unlock/win check
    const bool allCollected = (m_collectedCount >= totalTreasures());
    const float exitYaw = elapsedTime * (allCollected ? 2.4f : 0.8f);
    const float exitBob = allCollected ? (std::sin(elapsedTime * 3.0f) * 0.08f) : 0.0f;
    glm::mat4 exitModel = glm::translate(glm::mat4(1.0f), m_map->exitPos + glm::vec3(0.0f, 1.1f + exitBob, 0.0f));
    exitModel = glm::rotate(exitModel, exitYaw, glm::vec3(0.0f, 1.0f, 0.0f));
    exitModel = glm::rotate(exitModel, 0.55f, glm::vec3(1.0f, 0.0f, 1.0f));
    exitModel = glm::scale(exitModel, glm::vec3(0.50f, 0.85f, 0.50f));
    scene.updateObjectTransform(m_handles.exitObjectId, exitModel);

    if (m_handles.exitLightIndex < m_handles.pointLights.size())
    {
        render::GpuPointLight exitLight = m_handles.pointLights[m_handles.exitLightIndex];
        exitLight.colorIntensity.w = allCollected ? (4.6f + 1.4f * std::sin(elapsedTime * 3.6f)) : 2.6f;
        resources.updatePointLight(m_handles.exitLightIndex, exitLight);
    }

    if (!m_won && !m_flyMode)
    {
        const glm::vec2 exitXZ(m_map->exitPos.x, m_map->exitPos.z);
        const glm::vec2 d = playerXZ - exitXZ;
        if (glm::dot(d, d) <= kPickupRadius * kPickupRadius)
        {
            if (allCollected)
            {
                m_won = true;
            }
            else
            {
                m_lockedHintTimer = 2.0f;
            }
        }
    }
}

void GameSession::renderHud(int viewportWidth, int viewportHeight, float fps) const
{
    if (m_map == nullptr || viewportWidth <= 0 || viewportHeight <= 0)
    {
        return;
    }

    const float minDim = static_cast<float>(std::min(viewportWidth, viewportHeight));
    const float uiScale = std::clamp(minDim / 640.0f, 0.90f, 3.00f);
    ImGui::GetStyle().FontScaleMain = uiScale;

    const float margin = 14.0f * uiScale;
    const int total = totalTreasures();
    const bool allCollected = (m_collectedCount >= total);

    // Top-left compact HUD status card
    constexpr ImGuiWindowFlags kOverlayFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(11.0f * uiScale, 8.0f * uiScale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * uiScale, 5.0f * uiScale));

    ImGui::SetNextWindowPos(ImVec2(margin, margin), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.72f);
    if (ImGui::Begin("##MazeHud", nullptr, kOverlayFlags))
    {
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.42f, 1.0f), "TREASURES: %d / %d", m_collectedCount, total);
        ImGui::SameLine();
        ImGui::TextDisabled("  |  %.1fs  |  %.0f FPS", m_runTimer, fps);

        if (m_won)
        {
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.65f, 1.0f), "MAZE CLEARED! Press [R] for a new maze.");
        }
        else if (m_lockedHintTimer > 0.0f)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Exit locked: %d relic(s) remaining!", total - m_collectedCount);
        }
        else if (allCollected)
        {
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.65f, 1.0f), "All relics found! Reach the Emerald Exit.");
        }
        else
        {
            ImGui::TextUnformatted("Explore the dungeon & collect all golden relics.");
        }

        if (m_flyMode)
        {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "[FLY MODE] Collision & pickups paused");
        }
        ImGui::TextDisabled("WASD Move | Shift Sprint | Space Jump | M Map | F Fly | R New | F5 Shaders | F6 PBR/Clay | Esc Cursor");
    }
    ImGui::End();

    // Top-right Fog-of-War Minimap (scales proportionally with window height/width)
    const float mapSide = m_minimapExpanded ? std::clamp(minDim * 0.72f, 320.0f, 1200.0f) : std::clamp(minDim * 0.30f, 190.0f, 680.0f);
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(viewportWidth) - mapSide - margin, margin), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(mapSide, mapSide), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.76f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * uiScale, 8.0f * uiScale));
    if (ImGui::Begin("##Minimap", nullptr, kOverlayFlags))
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float cellPx = std::min(avail.x / static_cast<float>(m_map->size.x), avail.y / static_cast<float>(m_map->size.y));
        const float drawW = cellPx * static_cast<float>(m_map->size.x);
        const float drawH = cellPx * static_cast<float>(m_map->size.y);
        const ImVec2 gridOrigin(origin.x + (avail.x - drawW) * 0.5f, origin.y + (avail.y - drawH) * 0.5f);

        for (int y = 0; y < m_map->size.y; ++y)
        {
            for (int x = 0; x < m_map->size.x; ++x)
            {
                const bool seen = m_revealFullMap || (m_explored[static_cast<std::size_t>(m_map->getCellIndex(x, y))] != 0);
                if (!seen)
                {
                    continue;
                }
                const CellKind kind = m_map->getCell({x, y});
                ImU32 col = IM_COL32(78, 74, 68, 225);
                if (kind == CellKind::Wall)
                {
                    col = IM_COL32(28, 30, 38, 235);
                }
                else if (kind == CellKind::Courtyard)
                {
                    col = IM_COL32(126, 106, 74, 225);
                }
                else if (kind == CellKind::Exit)
                {
                    col = allCollected ? IM_COL32(45, 130, 95, 240) : IM_COL32(55, 90, 78, 230);
                }

                const ImVec2 p0(gridOrigin.x + static_cast<float>(x) * cellPx, gridOrigin.y + static_cast<float>(y) * cellPx);
                const ImVec2 p1(p0.x + cellPx + 0.5f, p0.y + cellPx + 0.5f);
                dl->AddRectFilled(p0, p1, col);
            }
        }

        const float worldToMap = cellPx / m_map->cellSize;
        auto toMapPos = [&](glm::vec3 worldPos) -> ImVec2
        {
            return ImVec2(gridOrigin.x + worldPos.x * worldToMap, gridOrigin.y + worldPos.z * worldToMap);
        };

        // Oases on minimap when explored
        for (const OasisSpawn &oasis : m_map->oases)
        {
            const glm::ivec2 oCell = m_map->worldToCell(oasis.center);
            if (!m_revealFullMap && (!m_map->inBounds(oCell) || m_explored[static_cast<std::size_t>(m_map->getCellIndex(oCell.x, oCell.y))] == 0))
            {
                continue;
            }
            const ImVec2 oc = toMapPos(oasis.center);
            dl->AddCircleFilled(oc, std::max(3.5f * uiScale, oasis.radiusX * worldToMap * 0.85f), IM_COL32(34, 170, 185, 235));
            dl->AddCircleFilled(ImVec2(oc.x + cellPx * 0.28f, oc.y - cellPx * 0.18f), std::max(2.2f * uiScale, cellPx * 0.28f), IM_COL32(66, 185, 80, 245));
        }

        // Discovered treasures
        for (std::size_t i = 0; i < m_map->treasurePositions.size(); ++i)
        {
            const glm::ivec2 tCell = m_map->worldToCell(m_map->treasurePositions[i]);
            const bool seen = m_revealFullMap || (m_map->inBounds(tCell) && m_explored[static_cast<std::size_t>(m_map->getCellIndex(tCell.x, tCell.y))] != 0);
            if (!seen)
            {
                continue;
            }
            const ImVec2 tc = toMapPos(m_map->treasurePositions[i]);
            const float r = std::max(3.2f * uiScale, cellPx * 0.34f);
            if (i < m_collected.size() && m_collected[i] != 0)
            {
                dl->AddCircle(tc, r * 0.7f, IM_COL32(140, 140, 140, 180), 8, 1.3f * uiScale);
            }
            else
            {
                dl->AddQuadFilled(ImVec2(tc.x, tc.y - r), ImVec2(tc.x + r, tc.y), ImVec2(tc.x, tc.y + r), ImVec2(tc.x - r, tc.y), IM_COL32(255, 212, 68, 255));
            }
        }

        // Discovered exit shrine
        const bool exitSeen = m_revealFullMap || (m_map->inBounds(m_map->exitCell) && m_explored[static_cast<std::size_t>(m_map->getCellIndex(m_map->exitCell.x, m_map->exitCell.y))] != 0);
        if (exitSeen)
        {
            const ImVec2 ec = toMapPos(m_map->exitPos);
            const float r = std::max(3.6f * uiScale, cellPx * 0.38f);
            dl->AddCircleFilled(ec, r, allCollected ? IM_COL32(65, 255, 165, 255) : IM_COL32(55, 175, 125, 220));
        }

        // Player dot & facing cone
        const ImVec2 pc = toMapPos(m_position);
        const float dirX = std::cos(m_yaw);
        const float dirY = std::sin(m_yaw);
        const float coneLen = std::max(8.0f * uiScale, cellPx * 0.95f);
        const float spread = 0.45f;
        const ImVec2 cLeft(pc.x + (dirX * std::cos(spread) - dirY * std::sin(spread)) * coneLen, pc.y + (dirX * std::sin(spread) + dirY * std::cos(spread)) * coneLen);
        const ImVec2 cRight(pc.x + (dirX * std::cos(-spread) - dirY * std::sin(-spread)) * coneLen, pc.y + (dirX * std::sin(-spread) + dirY * std::cos(-spread)) * coneLen);
        dl->AddTriangleFilled(pc, cLeft, cRight, IM_COL32(255, 120, 80, 110));
        dl->AddCircleFilled(pc, std::max(3.0f * uiScale, cellPx * 0.30f), IM_COL32(255, 95, 70, 255));
    }
    ImGui::End();
    ImGui::PopStyleVar();

    // Victory banner
    if (m_won)
    {
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(viewportWidth) * 0.5f, static_cast<float>(viewportHeight) * 0.42f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.84f);
        if (ImGui::Begin("##VictoryBanner", nullptr, kOverlayFlags))
        {
            ImGui::TextColored(ImVec4(0.38f, 0.96f, 0.68f, 1.0f), "MAZE CLEARED!");
            ImGui::Separator();
            ImGui::Text("Time: %.1f s   |   Relics: %d / %d", m_runTimer, m_collectedCount, total);
            ImGui::TextDisabled("Press [R] to generate a new dungeon");
        }
        ImGui::End();
    }

    ImGui::PopStyleVar(2);
}

} // namespace game
