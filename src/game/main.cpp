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

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
    const std::uint32_t crc = crc32Update(0u, std::span<const std::uint8_t>(out.data() + typeOffset, 4u + payload.size()));
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
        zlibData.insert(zlibData.end(), rawScanlines.begin() + static_cast<std::ptrdiff_t>(offset), rawScanlines.begin() + static_cast<std::ptrdiff_t>(offset + blockLen));
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

[[nodiscard]] std::vector<SmokeCameraShot> buildSmokeTestShots(const game::MazeMapData &map, const game::MazeSceneHandles &handles, const game::GameSession &session)
{
    std::vector<SmokeCameraShot> shots;
    shots.push_back({
        .filename = "01_start_corridor.png",
        .eye = session.eyePosition(),
        .target = session.eyePosition() + session.forwardDirection() * 4.0f,
    });

    auto isSightlineWalkable = [&](glm::vec3 a, glm::vec3 b) -> bool
    {
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
                if (nbKind != game::CellKind::Wall && nbKind != game::CellKind::Courtyard && map.isWalkable(nb) && map.getCell(back) == game::CellKind::Courtyard)
                {
                    const glm::vec3 portalFace = map.cellCenterWorld({x, y}, 2.0f) + glm::vec3(static_cast<float>(d.x), 0.0f, static_cast<float>(d.y)) * (map.cellSize * 0.5f);
                    const glm::vec3 eye = map.cellCenterWorld(back, 1.35f) + glm::vec3(-static_cast<float>(d.y) * 0.45f, 0.0f, static_cast<float>(d.x) * 0.45f);
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

    auto syncSceneGpuData = [&](const game::MazeMapData &mazeMap, const game::MazeSceneHandles &handles, float wallHeight)
    {
        std::vector<std::uint8_t> gridKinds(mazeMap.cells.size(), 1u);
        for (std::size_t i = 0; i < mazeMap.cells.size(); ++i)
        {
            const game::CellKind k = mazeMap.cells[i];
            gridKinds[i] = (k == game::CellKind::Wall) ? 0u : ((k == game::CellKind::Courtyard) ? 2u : 1u);
        }
        renderer.resources().setPointLights(handles.pointLights);
        renderer.resources().initGiGrid(mazeMap.size, mazeMap.cellSize, wallHeight, gridKinds);
    };

    const game::MazeGenDesc mapDesc{};
    const game::MazeSceneDesc sceneDesc{};

    game::MazeMapData map = game::generateMaze(mapDesc);
    game::MazeSceneHandles sceneHandles = game::buildMazeScene(map, renderer.resources().scene(), exeDir, sceneDesc, &renderer.resources().grass());
    syncSceneGpuData(map, sceneHandles, sceneDesc.wallHeight);

    game::GameSession session;
    session.reset(map, sceneHandles);

    std::uint32_t frameCount = 0;
    float smoothedFps = 60.0f;
    float fogDensity = 0.045f;
    float fogAnisotropy = 0.33f;
    bool whiteboxMode = false;

    const std::vector<SmokeCameraShot> smokeShots = smokeTest ? buildSmokeTestShots(map, sceneHandles, session) : std::vector<SmokeCameraShot>{};
    const std::filesystem::path screenshotDir = std::filesystem::current_path() / "screenshots";
    constexpr std::uint32_t kWarmupFramesPerShot = 40u;

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
        if (window.wasKeyPressed(GLFW_KEY_F6))
        {
            whiteboxMode = !whiteboxMode;
            std::cout << "[MaterialMode] " << (whiteboxMode ? "Whitebox Clay" : "Textured PBR") << '\n';
        }
        if (window.wasKeyPressed(GLFW_KEY_LEFT_BRACKET))
        {
            fogDensity = glm::clamp(fogDensity - 0.004f, 0.0f, 0.12f);
            std::cout << "[VolumetricFog] density = " << fogDensity << '\n';
        }
        if (window.wasKeyPressed(GLFW_KEY_RIGHT_BRACKET))
        {
            fogDensity = glm::clamp(fogDensity + 0.004f, 0.0f, 0.12f);
            std::cout << "[VolumetricFog] density = " << fogDensity << '\n';
        }
        if (window.wasKeyPressed(GLFW_KEY_MINUS))
        {
            fogAnisotropy = glm::clamp(fogAnisotropy - 0.05f, -0.80f, 0.85f);
            std::cout << "[VolumetricFog] anisotropy = " << fogAnisotropy << '\n';
        }
        if (window.wasKeyPressed(GLFW_KEY_EQUAL))
        {
            fogAnisotropy = glm::clamp(fogAnisotropy + 0.05f, -0.80f, 0.85f);
            std::cout << "[VolumetricFog] anisotropy = " << fogAnisotropy << '\n';
        }
        if (window.wasKeyPressed(GLFW_KEY_R))
        {
            renderer.resources().scene() = game::render::MegaScene{};
            renderer.resources().grass().clear();
            map = game::generateMaze(mapDesc);
            sceneHandles = game::buildMazeScene(map, renderer.resources().scene(), exeDir, sceneDesc, &renderer.resources().grass());
            syncSceneGpuData(map, sceneHandles, sceneDesc.wallHeight);
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

            glm::mat4 activeView = session.viewMatrix();
            glm::vec3 activeEye = session.eyePosition();
            std::size_t shotIdx = 0;
            if (smokeTest && !smokeShots.empty())
            {
                shotIdx = std::min<std::size_t>(frameCount / kWarmupFramesPerShot, smokeShots.size() - 1u);
                activeEye = smokeShots[shotIdx].eye;
                activeView = glm::lookAt(smokeShots[shotIdx].eye, smokeShots[shotIdx].target, glm::vec3(0.0f, 1.0f, 0.0f));
            }

            const game::render::RenderContext ctx{
                .view = activeView,
                .proj = proj,
                .cameraPos = activeEye,
                .nearPlane = nearZ,
                .farPlane = farZ,
                .elapsedTime = window.elapsedTime(),
                .deltaTime = dt,
                .frameIndex = frameCount,
                .viewportWidth = window.width(),
                .viewportHeight = window.height(),
                .fogDensity = fogDensity,
                .fogAnisotropy = fogAnisotropy,
                .whiteboxMode = whiteboxMode,
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
                ImGui::Render();
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            }
        }

        window.swapBuffers();

        ++frameCount;
        const std::uint32_t targetSmokeFrames = std::max<std::uint32_t>(120u, static_cast<std::uint32_t>(smokeShots.size()) * kWarmupFramesPerShot);
        if (smokeTest && frameCount >= targetSmokeFrames)
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
