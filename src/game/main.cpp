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

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#define DEFAULT_WINDOW_WIDTH 1920
#define DEFAULT_WINDOW_HEIGHT 1080

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

[[nodiscard]] std::uint32_t crc32Update(std::uint32_t crc, std::span<const std::uint8_t> data) noexcept
{
    crc = ~crc;
    for (std::uint8_t byte : data)
    {
        crc ^= byte;
        for (int b = 0; b < 8; ++b)
        {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
    }
    return ~crc;
}

void appendBe32(std::vector<std::uint8_t> &dst, std::uint32_t v)
{
    dst.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    dst.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    dst.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    dst.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

void appendPngChunk(std::vector<std::uint8_t> &out, const char type[4], std::span<const std::uint8_t> payload)
{
    appendBe32(out, static_cast<std::uint32_t>(payload.size()));
    const std::size_t typeOffset = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    const std::uint32_t crc =
        crc32Update(0u, std::span<const std::uint8_t>(out.data() + typeOffset, 4u + payload.size()));
    appendBe32(out, crc);
}

bool writeRgbPng(const std::filesystem::path &path, int width, int height, std::span<const std::uint8_t> bottomUpRgb)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    const std::size_t rowBytes = static_cast<std::size_t>(width) * 3u;
    std::vector<std::uint8_t> rawScanlines;
    rawScanlines.reserve(static_cast<std::size_t>(height) * (1u + rowBytes));
    std::uint32_t adlerA = 1u;
    std::uint32_t adlerB = 0u;

    for (int y = height - 1; y >= 0; --y)
    {
        rawScanlines.push_back(0u);
        adlerA = (adlerA + 0u) % 65521u;
        adlerB = (adlerB + adlerA) % 65521u;
        const std::uint8_t *srcRow = bottomUpRgb.data() + static_cast<std::size_t>(y) * rowBytes;
        rawScanlines.insert(rawScanlines.end(), srcRow, srcRow + rowBytes);
        for (std::size_t x = 0; x < rowBytes; ++x)
        {
            adlerA = (adlerA + srcRow[x]) % 65521u;
            adlerB = (adlerB + adlerA) % 65521u;
        }
    }

    // Wrap scanlines in standard zlib + stored-DEFLATE blocks (zero external dependencies)
    std::vector<std::uint8_t> zlibData{0x78u, 0x01u};
    std::size_t offset = 0;
    while (offset < rawScanlines.size())
    {
        const std::size_t blockLen = std::min<std::size_t>(65535u, rawScanlines.size() - offset);
        const bool isFinal = (offset + blockLen) >= rawScanlines.size();
        zlibData.push_back(isFinal ? 0x01u : 0x00u);
        const std::uint16_t len16 = static_cast<std::uint16_t>(blockLen);
        const std::uint16_t nlen16 = static_cast<std::uint16_t>(~len16);
        zlibData.push_back(static_cast<std::uint8_t>(len16 & 0xFFu));
        zlibData.push_back(static_cast<std::uint8_t>((len16 >> 8) & 0xFFu));
        zlibData.push_back(static_cast<std::uint8_t>(nlen16 & 0xFFu));
        zlibData.push_back(static_cast<std::uint8_t>((nlen16 >> 8) & 0xFFu));
        zlibData.insert(zlibData.end(), rawScanlines.begin() + static_cast<std::ptrdiff_t>(offset),
                        rawScanlines.begin() + static_cast<std::ptrdiff_t>(offset + blockLen));
        offset += blockLen;
    }
    appendBe32(zlibData, (adlerB << 16) | adlerA);

    std::vector<std::uint8_t> png{0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au, 0x0Au};
    std::vector<std::uint8_t> ihdr;
    appendBe32(ihdr, static_cast<std::uint32_t>(width));
    appendBe32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.insert(ihdr.end(), {8u, 2u, 0u, 0u, 0u});
    appendPngChunk(png, "IHDR", ihdr);
    appendPngChunk(png, "IDAT", zlibData);
    appendPngChunk(png, "IEND", {});

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return false;
    }
    file.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(file);
}

struct SmokeCameraShot
{
    std::string filename;
    glm::vec3 eye{0.0f};
    glm::vec3 target{0.0f};
};

[[nodiscard]] std::vector<SmokeCameraShot> buildSmokeTestShots(const game::MazeMapData &map,
                                                               const game::MazeSceneHandles &handles,
                                                               const game::GameSession &session)
{
    std::vector<SmokeCameraShot> shots;
    shots.push_back({
        .filename = "01_start_corridor.png",
        .eye = session.eyePosition(),
        .target = session.eyePosition() + session.forwardDirection() * 4.0f,
    });

    auto isSightlineWalkable = [&](glm::vec3 a, glm::vec3 b) -> bool {
        for (int s = 1; s <= 4; ++s)
        {
            const float t = static_cast<float>(s) / 5.0f;
            const glm::vec3 p = glm::mix(a, b, t);
            if (!map.isWalkable(map.worldToCell(p)))
            {
                return false;
            }
        }
        return true;
    };

    // Shot 2: Close-up of a stylized courtyard bush + flagstones + seam grass
    bool foundBushShot = false;
    for (const glm::vec3 &bushPos : handles.bushPositions)
    {
        for (int a = 0; a < 12; ++a)
        {
            const float ang = 0.5235988f * static_cast<float>(a);
            const glm::vec3 eye(bushPos.x + std::cos(ang) * 1.95f, 0.92f, bushPos.z + std::sin(ang) * 1.95f);
            if (isSightlineWalkable(bushPos, eye))
            {
                shots.push_back({
                    .filename = "02_courtyard_bush.png",
                    .eye = eye,
                    .target = bushPos + glm::vec3(0.0f, 0.22f, 0.0f),
                });
                foundBushShot = true;
                break;
            }
        }
        if (foundBushShot)
        {
            break;
        }
    }

    // Shot 3: Open courtyard overview framing greenery, masonry parapets & floor depth
    for (int y = 2; y < map.size.y - 2; ++y)
    {
        bool foundCourtyard = false;
        for (int x = 2; x < map.size.x - 2; ++x)
        {
            if (map.getCell({x, y}) != game::CellKind::Courtyard)
            {
                continue;
            }
            const glm::vec3 center = map.cellCenterWorld({x, y}, 0.0f);
            const glm::vec3 eye = center + glm::vec3(0.0f, 1.05f, 0.0f);
            const glm::vec3 target = handles.bushPositions.empty()
                                         ? (center + glm::vec3(3.0f, 0.35f, 2.0f))
                                         : (handles.bushPositions.front() + glm::vec3(0.0f, 0.35f, 0.0f));
            if (glm::length(glm::vec2(target.x - eye.x, target.z - eye.z)) > 1.5f)
            {
                shots.push_back({
                    .filename = "03_courtyard_overview.png",
                    .eye = eye,
                    .target = target,
                });
                const glm::vec3 moonAzimuth = glm::normalize(glm::vec3(0.45f, 0.0f, 0.35f));
                shots.push_back({
                    .filename = "04_courtyard_sky.png",
                    .eye = center + glm::vec3(0.0f, 1.55f, 0.0f) - moonAzimuth * 1.4f,
                    .target = center + glm::vec3(0.0f, 1.55f, 0.0f) + moonAzimuth * 4.0f + glm::vec3(0.0f, 3.6f, 0.0f),
                });
                foundCourtyard = true;
                break;
            }
        }
        if (foundCourtyard)
        {
            break;
        }
    }

    // Shot 5: Courtyard portal gatehouse framing stone arch, lintel, battlements & contact SSAO
    constexpr glm::ivec2 kDirs[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    bool foundPortal = false;
    for (int y = 2; y < map.size.y - 2 && !foundPortal; ++y)
    {
        for (int x = 2; x < map.size.x - 2 && !foundPortal; ++x)
        {
            if (map.getCell({x, y}) != game::CellKind::Courtyard)
            {
                continue;
            }
            for (const glm::ivec2 d : kDirs)
            {
                const glm::ivec2 nb = glm::ivec2{x, y} + d;
                const glm::ivec2 back = glm::ivec2{x, y} - d;
                if (!map.inBounds(nb) || !map.inBounds(back))
                {
                    continue;
                }
                const game::CellKind nbKind = map.getCell(nb);
                if (nbKind != game::CellKind::Wall && nbKind != game::CellKind::Courtyard && map.isWalkable(nb) &&
                    map.getCell(back) == game::CellKind::Courtyard)
                {
                    const glm::vec3 portalFace =
                        map.cellCenterWorld({x, y}, 2.0f) +
                        glm::vec3(static_cast<float>(d.x), 0.0f, static_cast<float>(d.y)) * (map.cellSize * 0.5f);
                    const glm::vec3 eye =
                        map.cellCenterWorld(back, 1.35f) +
                        glm::vec3(-static_cast<float>(d.y) * 0.45f, 0.0f, static_cast<float>(d.x) * 0.45f);
                    shots.push_back({
                        .filename = "05_courtyard_portal.png",
                        .eye = eye,
                        .target = portalFace,
                    });
                    foundPortal = true;
                    break;
                }
            }
        }
    }

    return shots;
}

void captureBackbufferPng(const std::filesystem::path &path, int width, int height)
{
    glFinish();
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    if (writeRgbPng(path, width, height, pixels))
    {
        std::cout << "[SmokeTest] Saved screenshot: " << path.generic_string() << '\n';
    }
}

void runGameLoop(game::Window &window, const std::filesystem::path &exeDir, bool smokeTest, bool probeDebug)
{
    if (!smokeTest)
    {
        std::cout << "OpenGL Vendor  : " << glGetString(GL_VENDOR) << '\n';
        std::cout << "OpenGL Renderer: " << glGetString(GL_RENDERER) << '\n';
        std::cout << "OpenGL Version : " << glGetString(GL_VERSION) << '\n';
    }

    ImGuiScope imgui(window.nativeHandle());

    const std::filesystem::path exeShaderDir = exeDir / "shaders";
    const std::filesystem::path shaderDir =
        std::filesystem::exists(exeShaderDir) ? exeShaderDir : (std::filesystem::current_path() / "shaders");
    game::render::Renderer renderer(window.width(), window.height(), shaderDir, !smokeTest);

    auto syncSceneGpuData = [&](const game::MazeMapData &mazeMap, const game::MazeSceneHandles &handles,
                                float wallHeight) {
        std::vector<std::uint8_t> gridKinds(mazeMap.cells.size(), 1u);
        for (std::size_t i = 0; i < mazeMap.cells.size(); ++i)
        {
            const game::CellKind k = mazeMap.cells[i];
            gridKinds[i] = (k == game::CellKind::Wall) ? 0u : ((k == game::CellKind::Courtyard) ? 2u : 1u);
        }
        renderer.resources().setPointLights(handles.pointLights, handles.torchLightCount);
        renderer.resources().setFlames(handles.flames);
        renderer.resources().initGiGrid(mazeMap.size, mazeMap.cellSize, wallHeight, gridKinds);
    };

    auto stageLabelFor = [](int stage) -> const char * {
        switch (stage)
        {
        case 1:
            return "Carving procedural dungeon topology...";
        case 2:
            return "Building stone architecture & props...";
        case 3:
            return "Growing courtyard flora & crawling vines...";
        case 4:
            return "Scattering instanced floor grass...";
        default:
            return "Preparing scene builders...";
        }
    };

    auto drawLoadingScreen = [&](const char *title, const char *subtitle, float progress, bool overlayMode) {
        const int winW = std::max(1, window.width());
        const int winH = std::max(1, window.height());
        const float minDim = static_cast<float>(std::min(winW, winH));
        const float uiScale = std::clamp(minDim / 720.0f, 0.85f, 2.50f);

        if (!overlayMode)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, winW, winH);
            glDisable(GL_DEPTH_TEST);
            glClearColor(0.035f, 0.045f, 0.070f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
        }

        if (overlayMode)
        {
            ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0.0f, 0.0f),
                                                          ImVec2(static_cast<float>(winW), static_cast<float>(winH)),
                                                          IM_COL32(8, 11, 18, 175));
        }

        const ImVec2 cardSize(460.0f * uiScale, 175.0f * uiScale);
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(winW) * 0.5f, static_cast<float>(winH) * 0.5f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(cardSize);
        ImGui::SetNextWindowBgAlpha(0.92f);
        if (ImGui::Begin("##LoadingCard", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::SetWindowFontScale(1.25f * uiScale * 0.72f);
            ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.45f, 1.0f), "%s", title);
            ImGui::Spacing();
            ImGui::SetWindowFontScale(uiScale * 0.78f);
            ImGui::TextUnformatted(subtitle);
            ImGui::Spacing();

            // Animated spinner indicator alongside the progress bar
            const float t = static_cast<float>(window.elapsedTime());
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float radius = 8.0f * uiScale;
            const ImVec2 center(p.x + radius + 2.0f, p.y + radius + 3.0f);
            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->PathArcTo(center, radius, t * 5.0f, t * 5.0f + 4.4f, 18);
            dl->PathStroke(IM_COL32(235, 185, 95, 230), 0, 2.5f * uiScale);

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + radius * 2.8f);
            ImGui::ProgressBar(std::clamp(progress, 0.0f, 1.0f), ImVec2(-1.0f, 22.0f * uiScale));
            ImGui::Spacing();
            ImGui::TextDisabled("Controls: WASD Move | Shift Sprint | Tab Debug | R New Maze | Esc Cursor");
        }
        ImGui::End();

        if (!overlayMode)
        {
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            window.swapBuffers();
        }
    };

    const game::MazeGenDesc mapDesc{};
    const game::MazeSceneDesc sceneDesc{};

    game::MazeMapData map;
    game::MazeSceneHandles sceneHandles;

    if (smokeTest)
    {
        map = game::generateMaze(mapDesc);
        sceneHandles =
            game::buildMazeScene(map, renderer.resources().scene(), exeDir, sceneDesc, &renderer.resources().grass());
        syncSceneGpuData(map, sceneHandles, sceneDesc.wallHeight);
    }
    else
    {
        window.setCursorCaptured(false);
        std::atomic<int> cpuMazeStage{0};
        auto cpuMatsFut = std::async(std::launch::async,
                                     []() { return game::render::RenderResources::generateStarterMaterialsCpu(); });
        auto cpuSceneFut = std::async(
            std::launch::async, [&]() { return game::buildMazeSceneCpu(mapDesc, sceneDesc, exeDir, &cpuMazeStage); });

        bool passesDone = false;
        bool matsUploaded = false;
        bool sceneUploaded = false;
        std::string lastPassName = "Initializing...";

        while (!window.shouldClose() && !sceneUploaded)
        {
            window.pollEvents();
            if (window.consumeResized() && window.width() > 0 && window.height() > 0)
            {
                renderer.onResize(window.width(), window.height());
            }

            if (!passesDone)
            {
                passesDone = !renderer.stepNextPassInit(lastPassName);
                const int doneSteps = renderer.completedPassInitSteps();
                const int totalSteps = std::max(1, renderer.totalPassInitSteps());
                const std::string msg = "Compiling GPU Shaders (" + std::to_string(doneSteps) + "/" +
                                        std::to_string(totalSteps) + "): " + lastPassName;
                const float prog = 0.05f + 0.45f * (static_cast<float>(doneSteps) / static_cast<float>(totalSteps));
                drawLoadingScreen("MazeGame // OpenGL 4.6 Loading", msg.c_str(), prog, false);
            }
            else if (!matsUploaded)
            {
                if (cpuMatsFut.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
                {
                    drawLoadingScreen("MazeGame // OpenGL 4.6 Loading", "Baking PBR material arrays on GPU...", 0.58f,
                                      false);
                    renderer.initMaterialsFromCpu(cpuMatsFut.get());
                    matsUploaded = true;
                }
                else
                {
                    drawLoadingScreen("MazeGame // OpenGL 4.6 Loading", "Generating procedural textures...", 0.52f,
                                      false);
                }
            }
            else
            {
                const int st = cpuMazeStage.load(std::memory_order_relaxed);
                const float prog = 0.62f + 0.08f * static_cast<float>(std::clamp(st, 0, 4));
                if (cpuSceneFut.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
                {
                    drawLoadingScreen("MazeGame // OpenGL 4.6 Loading", "Uploading geometry & initializing GI grid...",
                                      0.96f, false);
                    game::BuiltMazeSceneCpu built = cpuSceneFut.get();
                    map = std::move(built.map);
                    sceneHandles = std::move(built.handles);
                    renderer.resources().scene() = std::move(built.scene);
                    renderer.resources().scene().buildGpuBuffers();
                    renderer.resources().grass().uploadInstances(built.grassInstances);
                    syncSceneGpuData(map, sceneHandles, built.wallHeight);
                    sceneUploaded = true;
                }
                else
                {
                    drawLoadingScreen("MazeGame // OpenGL 4.6 Loading", stageLabelFor(st), prog, false);
                }
            }
        }

        if (!sceneUploaded)
        {
            cpuMatsFut.wait();
            cpuSceneFut.wait();
            return;
        }
        window.setCursorCaptured(true);
    }

    game::GameSession session;
    session.reset(map, sceneHandles);

    std::uint32_t frameCount = 0;
    float smoothedFps = 60.0f;
    float fogDensity = 0.045f;
    float fogAnisotropy = 0.33f;
    float fogFar = 56.0f;
    float renderScale = 1.0f;
    float moonAzimuthDeg = 37.9f;
    float moonElevationDeg = 25.1f;
    float probeDebugRadius = 0.11f;
    int lastRenderW = window.width();
    int lastRenderH = window.height();
    bool whiteboxMode = false;
    bool showDebugPanel = false;
    bool lastShaderReloadOk = true;
    std::string shaderStatusMsg;
    game::render::RenderFeatureFlags features{};
    features.enableProbeDebug = probeDebug;

    std::atomic<int> regenMazeStage{0};
    std::optional<std::future<game::BuiltMazeSceneCpu>> regenFuture;
    auto startAsyncRegenerateMaze = [&]() {
        if (regenFuture.has_value())
        {
            return;
        }
        regenMazeStage.store(0, std::memory_order_relaxed);
        regenFuture = std::async(
            std::launch::async, [&]() { return game::buildMazeSceneCpu(mapDesc, sceneDesc, exeDir, &regenMazeStage); });
    };

    const std::vector<SmokeCameraShot> smokeShots =
        smokeTest ? buildSmokeTestShots(map, sceneHandles, session) : std::vector<SmokeCameraShot>{};
    const std::filesystem::path screenshotDir = std::filesystem::current_path() / "screenshots";
    constexpr std::uint32_t kWarmupFramesPerShot = 40u;

    while (!window.shouldClose())
    {
        window.pollEvents();

        if (!smokeTest && window.wasKeyPressed(GLFW_KEY_TAB))
        {
            showDebugPanel = !showDebugPanel;
            window.setCursorCaptured(!showDebugPanel);
        }

        if (window.wasKeyPressed(GLFW_KEY_ESCAPE))
        {
            if (window.isCursorCaptured())
            {
                window.setCursorCaptured(false);
            }
            else if (showDebugPanel)
            {
                showDebugPanel = false;
            }
            else
            {
                window.requestClose();
            }
        }
        if (!window.isCursorCaptured() && window.isMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT) &&
            !ImGui::GetIO().WantCaptureMouse)
        {
            window.setCursorCaptured(true);
        }

        if (window.wasKeyPressed(GLFW_KEY_R))
        {
            startAsyncRegenerateMaze();
        }

        if (regenFuture.has_value() && regenFuture->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        {
            game::BuiltMazeSceneCpu built = regenFuture->get();
            regenFuture.reset();
            map = std::move(built.map);
            sceneHandles = std::move(built.handles);
            renderer.resources().scene() = std::move(built.scene);
            renderer.resources().scene().buildGpuBuffers();
            renderer.resources().grass().uploadInstances(built.grassInstances);
            syncSceneGpuData(map, sceneHandles, built.wallHeight);
            session.reset(map, sceneHandles);
        }

        if (window.consumeResized())
        {
            // Handled below via renderScale-adjusted render target sizing
        }

        const float dt = window.deltaTime();
        if (dt > 1e-5f)
        {
            smoothedFps = glm::mix(smoothedFps, 1.0f / dt, 0.08f);
        }
        if (!regenFuture.has_value())
        {
            session.update(window, dt, renderer.resources());
        }

        if (window.width() > 0 && window.height() > 0)
        {
            const float activeScale = smokeTest ? 1.0f : std::clamp(renderScale, 0.35f, 1.0f);
            const int renderW =
                std::max(1, static_cast<int>(std::lround(static_cast<float>(window.width()) * activeScale)));
            const int renderH =
                std::max(1, static_cast<int>(std::lround(static_cast<float>(window.height()) * activeScale)));
            if (renderW != lastRenderW || renderH != lastRenderH)
            {
                renderer.onResize(renderW, renderH);
                lastRenderW = renderW;
                lastRenderH = renderH;
            }

            constexpr float nearZ = 0.1f;
            constexpr float farZ = 150.0f;
            // Reversed-Z [0, 1] projection: near/far parameters intentionally swapped
            const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.0f), window.aspectRatio(), farZ, nearZ);

            glm::mat4 activeView = session.viewMatrix();
            glm::vec3 activeEye = session.eyePosition();
            std::size_t shotIdx = 0;
            if (smokeTest && !smokeShots.empty())
            {
                shotIdx = std::min<std::size_t>(frameCount / kWarmupFramesPerShot, smokeShots.size() - 1u);
                activeEye = smokeShots[shotIdx].eye;
                activeView =
                    glm::lookAt(smokeShots[shotIdx].eye, smokeShots[shotIdx].target, glm::vec3(0.0f, 1.0f, 0.0f));
            }

            const float azRad = glm::radians(moonAzimuthDeg);
            const float elRad = glm::radians(std::clamp(moonElevationDeg, 8.0f, 88.0f));
            const glm::vec3 moonDirection = glm::normalize(
                glm::vec3(std::cos(elRad) * std::cos(azRad), std::sin(elRad), std::cos(elRad) * std::sin(azRad)));

            const game::render::RenderContext ctx{
                .view = activeView,
                .proj = proj,
                .cameraPos = activeEye,
                .moonDirection = moonDirection,
                .nearPlane = nearZ,
                .farPlane = farZ,
                .elapsedTime = window.elapsedTime(),
                .deltaTime = dt,
                .frameIndex = frameCount,
                .viewportWidth = renderW,
                .viewportHeight = renderH,
                .presentWidth = window.width(),
                .presentHeight = window.height(),
                .fogDensity = fogDensity,
                .fogAnisotropy = fogAnisotropy,
                .fogFar = fogFar,
                .probeDebugRadius = probeDebugRadius,
                .whiteboxMode = whiteboxMode,
                .profilePasses = showDebugPanel,
                .features = features,
            };
            renderer.renderFrame(ctx);

            if (smokeTest && !smokeShots.empty() && ((frameCount + 1u) % kWarmupFramesPerShot) == 0u)
            {
                captureBackbufferPng(screenshotDir / smokeShots[shotIdx].filename, window.width(), window.height());
            }

            if (!smokeTest)
            {
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

                if (showDebugPanel)
                {
                    const float minDim = static_cast<float>(std::min(window.width(), window.height()));
                    const float uiScale = std::clamp(minDim / 640.0f, 0.90f, 3.00f);
                    const float margin = 14.0f * uiScale;
                    ImGui::SetNextWindowPos(ImVec2(margin, 130.0f * uiScale), ImGuiCond_FirstUseEver);
                    ImGui::SetNextWindowBgAlpha(0.84f);
                    if (ImGui::Begin("Render & Demo Panel", &showDebugPanel,
                                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
                    {
                        if (ImGui::Button("Reload Shaders"))
                        {
                            lastShaderReloadOk = renderer.reloadShaders();
                            shaderStatusMsg =
                                lastShaderReloadOk ? "Shaders reloaded." : "Shader reload failed (see console).";
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("New Maze"))
                        {
                            startAsyncRegenerateMaze();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("All On"))
                        {
                            features = game::render::RenderFeatureFlags{};
                        }

                        if (!shaderStatusMsg.empty())
                        {
                            const ImVec4 col = lastShaderReloadOk ? ImVec4(0.35f, 0.90f, 0.50f, 1.0f)
                                                                  : ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
                            ImGui::TextColored(col, "%s", shaderStatusMsg.c_str());
                        }

                        ImGui::Checkbox("Whitebox Clay Mode", &whiteboxMode);

                        if (ImGui::CollapsingHeader("Pipeline Features", ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            ImGui::Checkbox("Directional Shadow", &features.enableShadows);
                            ImGui::Checkbox("Point Shadows (Top-4)", &features.enablePointShadows);
                            ImGui::Checkbox("HZB Occlusion Culling", &features.enableHzb);
                            ImGui::Checkbox("Floor Puddles", &features.enablePuddles);
                            ImGui::Checkbox("SSAO", &features.enableSsao);
                            ImGui::Checkbox("3D Probe GI", &features.enableGi);
                            ImGui::Checkbox("Show GI Probes (L1 SH)", &features.enableProbeDebug);
                            ImGui::Checkbox("Indirect GI Only", &features.enableIndirectOnly);
                            ImGui::Checkbox("HDR Flames", &features.enableFlames);
                            ImGui::Checkbox("Planar Reflections (SSPR)", &features.enableSspr);
                            ImGui::Checkbox("Volumetric Fog", &features.enableFog);
                            ImGui::Checkbox("HDR Bloom", &features.enableBloom);
                            ImGui::Checkbox("FXAA", &features.enableFxaa);
                        }

                        if (ImGui::CollapsingHeader("Atmosphere & Scale", ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            ImGui::SliderFloat("Render Scale", &renderScale, 0.40f, 1.00f, "%.2fx");
                            if (ImGui::Button("0.50x"))
                            {
                                renderScale = 0.50f;
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("0.67x"))
                            {
                                renderScale = 0.67f;
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("0.75x"))
                            {
                                renderScale = 0.75f;
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("1.00x"))
                            {
                                renderScale = 1.00f;
                            }
                            ImGui::Separator();
                            ImGui::SliderFloat("Moon Azimuth", &moonAzimuthDeg, -180.0f, 180.0f, "%.1f deg");
                            ImGui::SliderFloat("Moon Elevation", &moonElevationDeg, 8.0f, 88.0f, "%.1f deg");
                            if (ImGui::Button("Reset Moon"))
                            {
                                moonAzimuthDeg = 37.9f;
                                moonElevationDeg = 56.1f;
                            }
                            if (features.enableProbeDebug)
                            {
                                ImGui::SliderFloat("GI Probe Radius", &probeDebugRadius, 0.05f, 0.25f, "%.2f m");
                            }
                            ImGui::Separator();
                            ImGui::SliderFloat("Fog Density", &fogDensity, 0.0f, 0.12f, "%.3f");
                            ImGui::SliderFloat("Fog Anisotropy", &fogAnisotropy, -0.80f, 0.85f, "%.2f");
                            ImGui::SliderFloat("Fog Far", &fogFar, 16.0f, 120.0f, "%.1f m");
                        }

                        if (ImGui::CollapsingHeader("GPU Pass Timings", ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            const auto &res = renderer.resources();
                            const glm::ivec2 hzbSz = res.hzbSize();
                            ImGui::Text("Render: %dx%d -> %dx%d | Total GPU: %.2f ms", renderW, renderH, window.width(),
                                        window.height(), renderer.totalGpuMs());
                            ImGui::Text("Scene: %u meshes, %u objs | HZB: %dx%d", res.scene().meshCount(),
                                        res.scene().objectCount(), hzbSz.x, hzbSz.y);
                            if (ImGui::BeginTable("##PassTimings", 2,
                                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                                      ImGuiTableFlags_SizingFixedFit))
                            {
                                ImGui::TableSetupColumn("Pass");
                                ImGui::TableSetupColumn("ms");
                                for (const auto &pt : renderer.passTimings())
                                {
                                    if (pt.gpuMs < 0.01f)
                                    {
                                        continue;
                                    }
                                    ImGui::TableNextRow();
                                    ImGui::TableSetColumnIndex(0);
                                    ImGui::TextUnformatted(pt.name.c_str());
                                    ImGui::TableSetColumnIndex(1);
                                    ImGui::Text("%.2f", pt.gpuMs);
                                }
                                ImGui::EndTable();
                            }
                        }
                    }
                    ImGui::End();
                }

                if (regenFuture.has_value())
                {
                    const int st = regenMazeStage.load(std::memory_order_relaxed);
                    const float prog = 0.15f + 0.20f * static_cast<float>(std::clamp(st, 0, 4));
                    drawLoadingScreen("Forging New Dungeon...", stageLabelFor(st), prog, true);
                }

                ImGui::Render();
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            }
        }

        window.swapBuffers();

        ++frameCount;
        const std::uint32_t targetSmokeFrames =
            std::max<std::uint32_t>(120u, static_cast<std::uint32_t>(smokeShots.size()) * kWarmupFramesPerShot);
        if (smokeTest && frameCount >= targetSmokeFrames)
        {
            break;
        }
    }

    if (regenFuture.has_value())
    {
        regenFuture->wait();
    }
}

} // namespace

int main(int argc, char **argv)
{
    bool smokeTest = false;
    bool probeDebug = false;
#ifdef NDEBUG
    bool glDebug = false;
#else
    bool glDebug = true;
#endif
    for (int i = 1; i < argc; ++i)
    {
        if (std::string_view(argv[i]) == "--smoke-test")
        {
            smokeTest = true;
        }
        else if (std::string_view(argv[i]) == "--probe-debug")
        {
            probeDebug = true;
        }
        else if (std::string_view(argv[i]) == "--gl-debug")
        {
            glDebug = true;
        }
    }

#ifdef _WIN32
    if ((smokeTest || glDebug) && GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_UNKNOWN)
    {
        if (AttachConsole(ATTACH_PARENT_PROCESS))
        {
            FILE *dummyOut = nullptr;
            FILE *dummyErr = nullptr;
            freopen_s(&dummyOut, "CONOUT$", "w", stdout);
            freopen_s(&dummyErr, "CONOUT$", "w", stderr);
        }
    }
#endif

    try
    {
        const std::filesystem::path exeDir = getExecutableDirectory(argc > 0 ? argv[0] : nullptr);
        game::WindowConfig cfg{
            .width = DEFAULT_WINDOW_WIDTH,
            .height = DEFAULT_WINDOW_HEIGHT,
            .title = "MazeGame (OpenGL 4.6 Core)",
            .vsync = !smokeTest,
            .hidden = smokeTest,
            .debugContext = glDebug || smokeTest,
        };
        game::Window window(cfg);
        runGameLoop(window, exeDir, smokeTest, probeDebug);

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
