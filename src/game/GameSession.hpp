#pragma once

#include "game/MazeGenerator.hpp"
#include "game/MazeSceneBuilder.hpp"
#include "game/Window.hpp"
#include "game/render/RenderResources.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace game
{

/// Manages FPS player movement, AABB/oasis sliding collision, treasure pickup, exit win state, exploration fog-of-war, and ImGui minimap/HUD.
class GameSession
{
public:
    void reset(const MazeMapData &map, const MazeSceneHandles &handles);
    void update(const Window &window, float dt, render::RenderResources &resources);
    void renderHud(int viewportWidth, int viewportHeight, float fps) const;

    [[nodiscard]] glm::vec3 eyePosition() const noexcept;
    [[nodiscard]] glm::vec3 forwardDirection() const noexcept;
    [[nodiscard]] glm::mat4 viewMatrix() const noexcept;

    [[nodiscard]] int collectedCount() const noexcept { return m_collectedCount; }
    [[nodiscard]] int totalTreasures() const noexcept { return static_cast<int>(m_collected.size()); }
    [[nodiscard]] bool hasWon() const noexcept { return m_won; }

private:
    void updateMovement(const Window &window, float dt);
    void resolveCollisions(glm::vec2 &xz) const noexcept;
    void updateExploration() noexcept;
    void updateWorldInteractions(float dt, float elapsedTime, render::RenderResources &resources);

    const MazeMapData *m_map = nullptr;
    MazeSceneHandles m_handles;

    glm::vec3 m_position{0.0f}; // Feet position in world space (y = 0 on floor)
    float m_verticalVel = 0.0f;
    float m_yaw = 0.0f;
    float m_pitch = -0.05f;
    float m_bobPhase = 0.0f;
    float m_bobOffset = 0.0f;
    bool m_grounded = true;
    bool m_flyMode = false;

    std::vector<std::uint8_t> m_collected;
    std::vector<std::uint8_t> m_explored;
    int m_collectedCount = 0;
    bool m_won = false;
    float m_runTimer = 0.0f;
    float m_lockedHintTimer = 0.0f;

    bool m_minimapExpanded = false;
    bool m_revealFullMap = false;
};

} // namespace game
