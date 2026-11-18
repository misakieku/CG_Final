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
#include "game/GameSession.hpp"
#include "game/MazeGenerator.hpp"
#include "game/MazeSceneBuilder.hpp"
#include "game/Window.hpp"
#include "game/render/Renderer.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

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

struct ImGuiScope
{
    explicit ImGuiScope(GLFWwindow *win)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;

        const std::filesystem::path segoeUi = "C:/Windows/Fonts/segoeui.ttf";
        if (std::filesystem::exists(segoeUi))
        {
            io.Fonts->AddFontFromFileTTF(segoeUi.string().c_str(), 17.0f);
        }
        else
        {
            io.Fonts->AddFontDefaultVector();
        }

        ImGuiStyle &style = ImGui::GetStyle();
        style.WindowRounding = 6.0f;
        style.WindowBorderSize = 1.0f;

        ImGui_ImplGlfw_InitForOpenGL(win, true);
        ImGui_ImplOpenGL3_Init("#version 460 core");
    }

    ~ImGuiScope()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    ImGuiScope(const ImGuiScope &) = delete;
    ImGuiScope &operator=(const ImGuiScope &) = delete;
};

void runGameLoop(game::Window &window, const std::filesystem::path &exeDir, bool smokeTest)
{
    if (!smokeTest)
    {
        std::cout << "OpenGL Vendor  : " << glGetString(GL_VENDOR) << '\n';
        std::cout << "OpenGL Renderer: " << glGetString(GL_RENDERER) << '\n';
        std::cout << "OpenGL Version : " << glGetString(GL_VERSION) << '\n';
    }

    ImGuiScope imgui(window.nativeHandle());

    const std::filesystem::path exeShaderDir = exeDir / "shaders";
    const std::filesystem::path shaderDir = std::filesystem::exists(exeShaderDir) ? exeShaderDir : (std::filesystem::current_path() / "shaders");
    game::render::Renderer renderer(window.width(), window.height(), shaderDir);

    const game::MazeGenDesc mapDesc{};
    const game::MazeSceneDesc sceneDesc{};

    game::MazeMapData map = game::generateMaze(mapDesc);
    game::MazeSceneHandles sceneHandles = game::buildMazeScene(map, renderer.resources().scene(), exeDir, sceneDesc);
    renderer.resources().setPointLights(sceneHandles.pointLights);

    game::GameSession session;
    session.reset(map, sceneHandles);

    std::uint32_t frameCount = 0;
    float smoothedFps = 60.0f;

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

        if (window.wasKeyPressed(GLFW_KEY_F5))
        {
            renderer.reloadShaders();
        }
        if (window.wasKeyPressed(GLFW_KEY_R))
        {
            renderer.resources().scene() = game::render::MegaScene{};
            map = game::generateMaze(mapDesc);
            sceneHandles = game::buildMazeScene(map, renderer.resources().scene(), exeDir, sceneDesc);
            renderer.resources().setPointLights(sceneHandles.pointLights);
            session.reset(map, sceneHandles);
        }

        if (window.consumeResized())
        {
            renderer.onResize(window.width(), window.height());
        }

        const float dt = window.deltaTime();
        if (dt > 1e-5f)
        {
            smoothedFps = glm::mix(smoothedFps, 1.0f / dt, 0.08f);
        }
        session.update(window, dt, renderer.resources());

        if (window.width() > 0 && window.height() > 0)
        {
            constexpr float nearZ = 0.1f;
            constexpr float farZ = 150.0f;
            // Reversed-Z [0, 1] projection: near/far parameters intentionally swapped
            const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.0f), window.aspectRatio(), farZ, nearZ);

            const game::render::RenderContext ctx{
                .view = session.viewMatrix(),
                .proj = proj,
                .cameraPos = session.eyePosition(),
                .nearPlane = nearZ,
                .farPlane = farZ,
                .elapsedTime = window.elapsedTime(),
                .deltaTime = dt,
                .frameIndex = frameCount,
                .viewportWidth = window.width(),
                .viewportHeight = window.height(),
            };
            renderer.renderFrame(ctx);

            ImGuiIO &io = ImGui::GetIO();
            if (window.isCursorCaptured())
            {
                io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
            }
            else
            {
                io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
            }
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            session.renderHud(window.width(), window.height(), smoothedFps);
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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
