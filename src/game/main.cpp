#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "common/gl_raii.hpp"
#include "game/MazeGenerator.hpp"
#include "game/MazeSceneBuilder.hpp"
#include "game/Window.hpp"
#include "game/render/Renderer.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace
{

[[nodiscard]] std::filesystem::path getExecutableDirectory(const char *argv0)
{
#ifdef _WIN32
    wchar_t buffer[MAX_PATH]{};
    const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
    {
        return std::filesystem::path(buffer).parent_path();
    }
#endif
    if (argv0 != nullptr)
    {
        std::filesystem::path p(argv0);
        if (p.has_parent_path())
        {
            return std::filesystem::absolute(p.parent_path());
        }
    }
    return std::filesystem::current_path();
}

struct FlyCamera
{
    glm::vec3 position{0.0f, 1.7f, 6.0f};
    float yaw = -glm::half_pi<float>();
    float pitch = -0.10f;
    float moveSpeed = 6.0f;
    float lookSensitivity = 0.0022f;

    [[nodiscard]] glm::vec3 forward() const noexcept
    {
        const float cp = std::cos(pitch);
        return glm::normalize(glm::vec3(cp * std::cos(yaw), std::sin(pitch), cp * std::sin(yaw)));
    }

    void update(const game::Window &window, float dt) noexcept
    {
        if (window.isCursorCaptured())
        {
            const glm::vec2 delta = window.mouseDelta();
            yaw += delta.x * lookSensitivity;
            pitch = std::clamp(pitch - delta.y * lookSensitivity, -1.48f, 1.48f);
        }

        const glm::vec3 fwd = forward();
        const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 move(0.0f);

        if (window.isKeyDown(GLFW_KEY_W))
        {
            move += fwd;
        }
        if (window.isKeyDown(GLFW_KEY_S))
        {
            move -= fwd;
        }
        if (window.isKeyDown(GLFW_KEY_D))
        {
            move += right;
        }
        if (window.isKeyDown(GLFW_KEY_A))
        {
            move -= right;
        }
        if (window.isKeyDown(GLFW_KEY_SPACE))
        {
            move.y += 1.0f;
        }
        if (window.isKeyDown(GLFW_KEY_LEFT_SHIFT))
        {
            move.y -= 1.0f;
        }

        if (glm::dot(move, move) > 1e-6f)
        {
            position += glm::normalize(move) * (moveSpeed * dt);
        }
    }

    [[nodiscard]] glm::mat4 viewMatrix() const noexcept
    {
        return glm::lookAt(position, position + forward(), glm::vec3(0.0f, 1.0f, 0.0f));
    }
};

[[nodiscard]] float findInitialLookYaw(const game::MazeMapData &map) noexcept
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

void runGameLoop(game::Window &window, const std::filesystem::path &exeDir, bool smokeTest)
{
    if (!smokeTest)
    {
        std::cout << "OpenGL Vendor  : " << glGetString(GL_VENDOR) << '\n';
        std::cout << "OpenGL Renderer: " << glGetString(GL_RENDERER) << '\n';
        std::cout << "OpenGL Version : " << glGetString(GL_VERSION) << '\n';
    }

    game::render::Renderer renderer(window.width(), window.height());

    game::MazeMapData map = game::generateMaze({21, 21}, 3.0f, {1, 1});
    game::MazeSceneHandles sceneHandles = game::buildMazeScene(map, renderer.resources().scene(), exeDir);
    renderer.resources().setPointLights(sceneHandles.pointLights);

    FlyCamera camera;
    camera.position = map.playerStart + glm::vec3(0.0f, 1.7f, 0.0f);
    camera.yaw = findInitialLookYaw(map);
    camera.pitch = -0.05f;

    std::uint32_t frameCount = 0;

    while (!window.shouldClose())
    {
        window.pollEvents();

        if (window.wasKeyPressed(GLFW_KEY_ESCAPE))
        {
            if (window.isCursorCaptured())
            {
                window.setCursorCaptured(false);
            }
            else
            {
                window.requestClose();
            }
        }
        if (!window.isCursorCaptured() && window.isMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT))
        {
            window.setCursorCaptured(true);
        }

        if (window.wasKeyPressed(GLFW_KEY_R))
        {
            renderer.resources().scene() = game::render::MegaScene{};
            map = game::generateMaze({21, 21}, 3.0f, {1, 1});
            sceneHandles = game::buildMazeScene(map, renderer.resources().scene(), exeDir);
            renderer.resources().setPointLights(sceneHandles.pointLights);
            camera.position = map.playerStart + glm::vec3(0.0f, 1.7f, 0.0f);
            camera.yaw = findInitialLookYaw(map);
            camera.pitch = -0.05f;
        }

        if (window.consumeResized())
        {
            renderer.onResize(window.width(), window.height());
        }

        camera.update(window, window.deltaTime());

        if (window.width() > 0 && window.height() > 0)
        {
            constexpr float nearZ = 0.1f;
            constexpr float farZ = 150.0f;
            // Reversed-Z [0, 1] projection: near/far parameters intentionally swapped
            const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.0f), window.aspectRatio(), farZ, nearZ);

            const game::render::RenderContext ctx{
                .view = camera.viewMatrix(),
                .proj = proj,
                .cameraPos = camera.position,
                .nearPlane = nearZ,
                .farPlane = farZ,
                .elapsedTime = window.elapsedTime(),
                .deltaTime = window.deltaTime(),
                .frameIndex = frameCount,
                .viewportWidth = window.width(),
                .viewportHeight = window.height(),
            };
            renderer.renderFrame(ctx);
        }

        window.swapBuffers();

        ++frameCount;
        if (smokeTest && frameCount >= 120)
        {
            break;
        }
    }
}

} // namespace

int main(int argc, char **argv)
{
    bool smokeTest = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string_view(argv[i]) == "--smoke-test")
        {
            smokeTest = true;
        }
    }

    try
    {
        const std::filesystem::path exeDir = getExecutableDirectory(argc > 0 ? argv[0] : nullptr);
        game::WindowConfig cfg{
            .width = 1280,
            .height = 720,
            .title = "MazeGame (OpenGL 4.6 Core)",
            .vsync = !smokeTest,
            .hidden = smokeTest,
            .debugContext = true,
        };
        game::Window window(cfg);
        runGameLoop(window, exeDir, smokeTest);

        if (!gl::checkNoErrors("MazeGame"))
        {
            return 1;
        }
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Fatal error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
