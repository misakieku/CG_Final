#include "common/dat_mesh.hpp"
#include "common/gl_raii.hpp"
#include "modeller/profile_curve.hpp"
#include "modeller/sor_mesh.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

constexpr const char *kMeshVertSrc = R"glsl(
#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMat;

out vec3 vWorldPos;
out vec3 vWorldNormal;

void main()
{
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vWorldNormal = normalize(uNormalMat * aNormal);
    gl_Position = uProj * uView * worldPos;
}
)glsl";

constexpr const char *kMeshFragSrc = R"glsl(
#version 460 core
in vec3 vWorldPos;
in vec3 vWorldNormal;

uniform vec3 uCameraPos;
uniform vec3 uBaseColor;
uniform int uMode; // 0 = Blinn-Phong, 1 = Normals, 2 = Wireframe tint

out vec4 FragColor;

vec3 ACESFilm(vec3 x)
{
    float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    if (uMode == 2)
    {
        vec3 wire = ACESFilm(vec3(0.08, 0.72, 0.92));
        FragColor = vec4(pow(wire, vec3(1.0 / 2.2)), 1.0);
        return;
    }

    vec3 N = normalize(vWorldNormal);
    if (!gl_FrontFacing)
    {
        N = -N;
    }

    if (uMode == 1)
    {
        vec3 vis = N * 0.5 + 0.5;
        FragColor = vec4(vis, 1.0);
        return;
    }

    vec3 V = normalize(uCameraPos - vWorldPos);

    // Key warm directional light + cool fill light
    vec3 L1 = normalize(vec3(2.5, 4.0, 3.0));
    vec3 H1 = normalize(L1 + V);
    float diff1 = max(dot(N, L1), 0.0);
    float spec1 = pow(max(dot(N, H1), 0.0), 48.0);

    vec3 L2 = normalize(vec3(-3.0, 1.5, -2.5));
    float diff2 = max(dot(N, L2), 0.0);

    vec3 ambient = 0.10 * uBaseColor;
    vec3 keyLight = (diff1 * uBaseColor + spec1 * vec3(0.45)) * vec3(1.05, 0.98, 0.90);
    vec3 fillLight = diff2 * uBaseColor * vec3(0.28, 0.36, 0.48);

    vec3 color = ambient + keyLight + fillLight;
    color = ACESFilm(color);
    FragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
)glsl";

constexpr const char *kLineVertSrc = R"glsl(
#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;

uniform mat4 uViewProj;
out vec3 vColor;

void main()
{
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)glsl";

constexpr const char *kLineFragSrc = R"glsl(
#version 460 core
in vec3 vColor;
out vec4 FragColor;

void main()
{
    FragColor = vec4(vColor, 1.0);
}
)glsl";

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

struct OrbitCamera
{
    float yaw = 0.65f;
    float pitch = 0.32f;
    float distance = 2.8f;
    float targetY = 0.0f;

    [[nodiscard]] glm::vec3 eyePosition() const noexcept
    {
        const float cp = std::cos(pitch);
        return {
            distance * cp * std::sin(yaw),
            targetY + distance * std::sin(pitch),
            distance * cp * std::cos(yaw),
        };
    }

    [[nodiscard]] glm::mat4 viewMatrix() const noexcept
    {
        return glm::lookAt(eyePosition(), glm::vec3(0.0f, targetY, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    void reset() noexcept
    {
        yaw = 0.65f;
        pitch = 0.32f;
        distance = 2.8f;
        targetY = 0.0f;
    }
};

struct ViewportFbo
{
    gl::Framebuffer fbo;
    gl::Texture colorTex;
    int width = 0;
    int height = 0;

    void ensureSize(int w, int h)
    {
        w = std::max(1, w);
        h = std::max(1, h);
        if (fbo && w == width && h == height)
        {
            return;
        }
        width = w;
        height = h;
        if (!fbo)
        {
            fbo = gl::Framebuffer::create();
        }
        colorTex.create2D(gl::Texture2DDesc{
            .width = width,
            .height = height,
            .internalFormat = GL_RGBA8,
            .format = GL_RGBA,
            .type = GL_UNSIGNED_BYTE,
            .minFilter = GL_LINEAR,
            .magFilter = GL_LINEAR,
            .wrapS = GL_CLAMP_TO_EDGE,
            .wrapT = GL_CLAMP_TO_EDGE,
        });
        fbo.attachTexture2D(GL_COLOR_ATTACHMENT0, colorTex);
        fbo.attachDepthRenderbuffer(width, height, GL_DEPTH24_STENCIL8);
        fbo.ensureComplete();
    }
};

struct MeshVertex
{
    glm::vec3 pos;
    glm::vec3 normal;
};

struct LineVertex
{
    glm::vec3 pos;
    glm::vec3 color;
};

struct GpuMesh
{
    gl::VertexArray vao;
    gl::Buffer vbo;
    gl::Buffer ebo;
    GLsizei indexCount = 0;

    void upload(const cg::DatMeshData &data)
    {
        if (!vao)
        {
            vao = gl::VertexArray::create();
            vbo = gl::Buffer::create();
            ebo = gl::Buffer::create();
        }

        std::vector<MeshVertex> vertices(data.positions.size());
        for (std::size_t i = 0; i < data.positions.size(); ++i)
        {
            const glm::vec3 n = (i < data.normals.size()) ? data.normals[i] : glm::vec3(0.0f, 1.0f, 0.0f);
            vertices[i] = MeshVertex{data.positions[i], n};
        }

        vao.bind();
        vbo.upload<MeshVertex>(GL_ARRAY_BUFFER, vertices, GL_DYNAMIC_DRAW);
        ebo.upload<std::uint32_t>(GL_ELEMENT_ARRAY_BUFFER, data.indices, GL_DYNAMIC_DRAW);
        vao.setAttribute(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), offsetof(MeshVertex, pos));
        vao.setAttribute(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), offsetof(MeshVertex, normal));
        gl::VertexArray::unbind();

        indexCount = static_cast<GLsizei>(data.indices.size());
    }
};

[[nodiscard]] std::vector<LineVertex> buildGridAndAxesLines()
{
    std::vector<LineVertex> lines;
    constexpr float floorY = -0.82f;
    constexpr int halfCells = 6;
    constexpr float step = 0.25f;
    constexpr float extent = halfCells * step;

    for (int i = -halfCells; i <= halfCells; ++i)
    {
        const float coord = static_cast<float>(i) * step;
        const glm::vec3 c = (i == 0) ? glm::vec3(0.32f) : glm::vec3(0.18f);
        lines.push_back({{coord, floorY, -extent}, c});
        lines.push_back({{coord, floorY, extent}, c});
        lines.push_back({{-extent, floorY, coord}, c});
        lines.push_back({{extent, floorY, coord}, c});
    }

    // Center revolution Y-axis + origin X/Z axis hints
    lines.push_back({{0.0f, -1.0f, 0.0f}, {0.25f, 0.85f, 0.35f}});
    lines.push_back({{0.0f, 1.0f, 0.0f}, {0.25f, 0.85f, 0.35f}});
    lines.push_back({{0.0f, floorY, 0.0f}, {0.85f, 0.25f, 0.25f}});
    lines.push_back({{0.6f, floorY, 0.0f}, {0.85f, 0.25f, 0.25f}});
    lines.push_back({{0.0f, floorY, 0.0f}, {0.25f, 0.45f, 0.90f}});
    lines.push_back({{0.0f, floorY, 0.6f}, {0.25f, 0.45f, 0.90f}});
    return lines;
}

class SorModellerApp
{
public:
    explicit SorModellerApp(const std::filesystem::path &exeDir)
        : m_meshShader(gl::Shader::compile(kMeshVertSrc, kMeshFragSrc)),
          m_lineShader(gl::Shader::compile(kLineVertSrc, kLineFragSrc)),
          m_controlPoints(modeller::makePresetProfile(modeller::ProfilePreset::Chalice))
    {
        const std::string defaultOut = (exeDir / "myModel.dat").string();
        std::snprintf(m_exportPathBuf, sizeof(m_exportPathBuf), "%s", defaultOut.c_str());

        const auto gridLines = buildGridAndAxesLines();
        m_gridVao = gl::VertexArray::create();
        m_gridVbo = gl::Buffer::create();
        m_gridVao.bind();
        m_gridVbo.upload<LineVertex>(GL_ARRAY_BUFFER, gridLines, GL_STATIC_DRAW);
        m_gridVao.setAttribute(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), offsetof(LineVertex, pos));
        m_gridVao.setAttribute(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), offsetof(LineVertex, color));
        gl::VertexArray::unbind();
        m_gridVertexCount = static_cast<GLsizei>(gridLines.size());

        rebuildMesh();
    }

    void renderFrame(int windowW, int windowH)
    {
        ImGuiViewport *mainVp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainVp->WorkPos);
        ImGui::SetNextWindowSize(mainVp->WorkSize);
        constexpr ImGuiWindowFlags rootFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
        ImGui::Begin("SOR_Modeller_Root", nullptr, rootFlags);
        ImGui::PopStyleVar();

        const float totalWidth = ImGui::GetContentRegionAvail().x;
        const float leftWidth = std::max(340.0f, totalWidth * 0.44f);

        ImGui::BeginChild("LeftProfilePane", ImVec2(leftWidth, 0.0f), ImGuiChildFlags_Borders);
        drawLeftPane();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("RightViewportPane", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        drawRightPane();
        ImGui::EndChild();

        ImGui::End();

        if (m_meshDirty)
        {
            rebuildMesh();
        }

        // Clear main backbuffer before ImGui renders
        gl::Framebuffer::bindDefault();
        glViewport(0, 0, windowW, windowH);
        glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

private:
    void rebuildMesh()
    {
        m_segments = modeller::buildPiecewiseBezier(m_controlPoints, m_smoothness);
        m_samples = modeller::sampleProfileCurve(m_segments, m_samplesPerSegment);
        m_cpuMesh = modeller::buildSorMesh(m_samples, m_radialSegments);
        m_gpuMesh.upload(m_cpuMesh);
        m_meshDirty = false;
    }

    void drawLeftPane()
    {
        ImGui::TextUnformatted("2D Profile Canvas (Piecewise Cubic Bezier)");
        ImGui::Separator();

        if (ImGui::Button("Chalice"))
        {
            m_controlPoints = modeller::makePresetProfile(modeller::ProfilePreset::Chalice);
            m_meshDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Vase"))
        {
            m_controlPoints = modeller::makePresetProfile(modeller::ProfilePreset::Vase);
            m_meshDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Pillar"))
        {
            m_controlPoints = modeller::makePresetProfile(modeller::ProfilePreset::Pillar);
            m_meshDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Candle"))
        {
            m_controlPoints = modeller::makePresetProfile(modeller::ProfilePreset::Candle);
            m_meshDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Undo") && !m_controlPoints.empty())
        {
            m_controlPoints.pop_back();
            m_dragIndex = -1;
            m_meshDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            m_controlPoints.clear();
            m_dragIndex = -1;
            m_meshDirty = true;
        }

        if (ImGui::SliderInt("Radial Segments (N)", &m_radialSegments, 6, 96))
        {
            m_meshDirty = true;
        }
        if (ImGui::SliderInt("Curve Samples / Seg", &m_samplesPerSegment, 2, 24))
        {
            m_meshDirty = true;
        }
        if (ImGui::SliderFloat("Bezier Smoothness", &m_smoothness, 0.0f, 1.0f, "%.2f"))
        {
            m_meshDirty = true;
        }

        ImGui::TextDisabled("Left-click: add/drag point | Right-click point: delete | r >= 0");

        const float exportBlockHeight = 92.0f;
        const ImVec2 canvasAvail = ImGui::GetContentRegionAvail();
        const ImVec2 canvasSize(std::max(100.0f, canvasAvail.x), std::max(160.0f, canvasAvail.y - exportBlockHeight));
        drawProfileCanvas(canvasSize);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::SetNextItemWidth(-135.0f);
        ImGui::InputText("##ExportPath", m_exportPathBuf, sizeof(m_exportPathBuf));
        ImGui::SameLine();
        if (ImGui::Button("Export .dat", ImVec2(125.0f, 0.0f)))
        {
            std::string err;
            if (cg::saveDatMesh(m_exportPathBuf, m_cpuMesh, &err))
            {
                m_exportStatusOk = true;
                m_exportStatus = "Exported " + std::to_string(m_cpuMesh.positions.size()) + " verts / " +
                                std::to_string(m_cpuMesh.indices.size() / 3) + " tris.";
            }
            else
            {
                m_exportStatusOk = false;
                m_exportStatus = err;
            }
        }
        if (!m_exportStatus.empty())
        {
            const ImVec4 col = m_exportStatusOk ? ImVec4(0.35f, 0.90f, 0.45f, 1.0f) : ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
            ImGui::TextColored(col, "%s", m_exportStatus.c_str());
        }
    }

    void drawProfileCanvas(const ImVec2 &size)
    {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 maxPos(origin.x + size.x, origin.y + size.y);

        ImGui::InvisibleButton("##ProfileCanvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool isHovered = ImGui::IsItemHovered();

        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(origin, maxPos, true);
        dl->AddRectFilled(origin, maxPos, IM_COL32(22, 24, 30, 255));

        const float worldHalfY = 1.05f;
        const float pixelsPerUnit = (size.y * 0.5f) / worldHalfY;
        const float axisScreenX = origin.x + 0.14f * size.x;
        const float axisScreenY = origin.y + size.y * 0.5f;

        auto toScreen = [&](glm::vec2 p) -> ImVec2
        {
            return ImVec2(axisScreenX + p.x * pixelsPerUnit, axisScreenY - p.y * pixelsPerUnit);
        };
        auto toWorld = [&](ImVec2 s) -> glm::vec2
        {
            return glm::vec2((s.x - axisScreenX) / pixelsPerUnit, (axisScreenY - s.y) / pixelsPerUnit);
        };

        dl->AddRectFilled(origin, ImVec2(axisScreenX, maxPos.y), IM_COL32(36, 22, 24, 180));

        for (int gx = 0; gx <= 12; ++gx)
        {
            const float wx = static_cast<float>(gx) * 0.25f;
            const float sx = axisScreenX + wx * pixelsPerUnit;
            if (sx > maxPos.x)
            {
                break;
            }
            dl->AddLine(ImVec2(sx, origin.y), ImVec2(sx, maxPos.y), IM_COL32(45, 50, 62, 255));
        }
        for (int gy = -4; gy <= 4; ++gy)
        {
            const float wy = static_cast<float>(gy) * 0.25f;
            const float sy = axisScreenY - wy * pixelsPerUnit;
            dl->AddLine(ImVec2(origin.x, sy), ImVec2(maxPos.x, sy), IM_COL32(45, 50, 62, 255));
        }

        dl->AddLine(ImVec2(origin.x, axisScreenY), ImVec2(maxPos.x, axisScreenY), IM_COL32(110, 75, 75, 255), 1.5f);
        dl->AddLine(ImVec2(axisScreenX, origin.y), ImVec2(axisScreenX, maxPos.y), IM_COL32(75, 210, 120, 255), 2.0f);
        dl->AddText(ImVec2(axisScreenX + 6.0f, origin.y + 6.0f), IM_COL32(110, 230, 145, 255), "Y Axis (r=0)");

        const ImVec2 mousePos = ImGui::GetIO().MousePos;
        int hoveredIdx = -1;
        float bestDistSq = 12.0f * 12.0f;
        for (int i = 0; i < static_cast<int>(m_controlPoints.size()); ++i)
        {
            const ImVec2 sp = toScreen(m_controlPoints[static_cast<std::size_t>(i)]);
            const float dx = mousePos.x - sp.x;
            const float dy = mousePos.y - sp.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < bestDistSq)
            {
                bestDistSq = d2;
                hoveredIdx = i;
            }
        }

        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (hoveredIdx >= 0)
            {
                m_dragIndex = hoveredIdx;
            }
            else
            {
                glm::vec2 wp = toWorld(mousePos);
                wp.x = std::max(0.0f, wp.x);
                wp.y = std::clamp(wp.y, -1.0f, 1.0f);
                m_controlPoints.push_back(wp);
                m_dragIndex = static_cast<int>(m_controlPoints.size()) - 1;
                m_meshDirty = true;
            }
        }

        if (m_dragIndex >= 0)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                glm::vec2 wp = toWorld(mousePos);
                wp.x = std::max(0.0f, wp.x);
                wp.y = std::clamp(wp.y, -1.0f, 1.0f);
                if (wp != m_controlPoints[static_cast<std::size_t>(m_dragIndex)])
                {
                    m_controlPoints[static_cast<std::size_t>(m_dragIndex)] = wp;
                    m_meshDirty = true;
                }
            }
            else
            {
                m_dragIndex = -1;
            }
        }

        if (isHovered && hoveredIdx >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            m_controlPoints.erase(m_controlPoints.begin() + hoveredIdx);
            m_dragIndex = -1;
            hoveredIdx = -1;
            m_meshDirty = true;
        }

        for (std::size_t i = 0; i + 1 < m_controlPoints.size(); ++i)
        {
            dl->AddLine(toScreen(m_controlPoints[i]), toScreen(m_controlPoints[i + 1]), IM_COL32(110, 120, 145, 160), 1.0f);
        }

        for (std::size_t i = 0; i + 1 < m_samples.size(); ++i)
        {
            dl->AddLine(toScreen(m_samples[i].position), toScreen(m_samples[i + 1].position), IM_COL32(70, 215, 255, 255), 2.5f);
        }

        for (int i = 0; i < static_cast<int>(m_controlPoints.size()); ++i)
        {
            const ImVec2 sp = toScreen(m_controlPoints[static_cast<std::size_t>(i)]);
            const bool active = (i == m_dragIndex || i == hoveredIdx);
            const ImU32 fill = active ? IM_COL32(255, 210, 60, 255) : IM_COL32(235, 240, 250, 255);
            dl->AddCircleFilled(sp, active ? 6.5f : 5.0f, fill);
            dl->AddCircle(sp, active ? 6.5f : 5.0f, IM_COL32(20, 25, 35, 255), 0, 1.5f);
        }

        if (isHovered && hoveredIdx >= 0)
        {
            const glm::vec2 p = m_controlPoints[static_cast<std::size_t>(hoveredIdx)];
            ImGui::SetTooltip("P%d: r=%.3f, y=%.3f", hoveredIdx, p.x, p.y);
        }

        dl->PopClipRect();
        dl->AddRect(origin, maxPos, IM_COL32(70, 75, 90, 255));
    }

    void drawRightPane()
    {
        ImGui::Checkbox("Wireframe", &m_wireframe);
        ImGui::SameLine();
        ImGui::Checkbox("Show Normals", &m_showNormals);
        ImGui::SameLine();
        ImGui::Checkbox("Backface Cull", &m_backfaceCull);
        ImGui::SameLine();
        ImGui::Checkbox("Grid", &m_showGrid);
        ImGui::SameLine();
        if (ImGui::Button("Reset Cam"))
        {
            m_camera.reset();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("| V: %zu  F: %zu", m_cpuMesh.positions.size(), m_cpuMesh.indices.size() / 3);
        ImGui::Separator();

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const int vpW = std::max(1, static_cast<int>(avail.x));
        const int vpH = std::max(1, static_cast<int>(avail.y));
        render3DViewport(vpW, vpH);

        ImGui::Image(static_cast<ImTextureID>(static_cast<std::intptr_t>(m_viewportFbo.colorTex.id())),
                     ImVec2(static_cast<float>(vpW), static_cast<float>(vpH)),
                     ImVec2(0.0f, 1.0f),
                     ImVec2(1.0f, 0.0f));

        if (ImGui::IsItemHovered())
        {
            const ImGuiIO &io = ImGui::GetIO();
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                m_camera.yaw -= io.MouseDelta.x * 0.008f;
                m_camera.pitch = std::clamp(m_camera.pitch + io.MouseDelta.y * 0.008f, -1.45f, 1.45f);
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
            {
                m_camera.targetY = std::clamp(m_camera.targetY + io.MouseDelta.y * 0.004f, -1.5f, 1.5f);
            }
            if (io.MouseWheel != 0.0f)
            {
                m_camera.distance = std::clamp(m_camera.distance * std::pow(0.90f, io.MouseWheel), 0.7f, 10.0f);
            }
        }
    }

    void render3DViewport(int width, int height)
    {
        m_viewportFbo.ensureSize(width, height);
        m_viewportFbo.fbo.bind();
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.09f, 0.10f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        const glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.05f, 50.0f);
        const glm::mat4 view = m_camera.viewMatrix();
        const glm::mat4 model(1.0f);

        if (m_showGrid)
        {
            m_lineShader.use();
            m_lineShader.setMat4("uViewProj", proj * view);
            m_gridVao.bind();
            glDrawArrays(GL_LINES, 0, m_gridVertexCount);
        }

        if (m_gpuMesh.indexCount > 0)
        {
            if (m_backfaceCull)
            {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }
            else
            {
                glDisable(GL_CULL_FACE);
            }

            m_meshShader.use();
            m_meshShader.setMat4("uModel", model);
            m_meshShader.setMat4("uView", view);
            m_meshShader.setMat4("uProj", proj);
            m_meshShader.setMat3("uNormalMat", glm::mat3(glm::transpose(glm::inverse(model))));
            m_meshShader.setVec3("uCameraPos", m_camera.eyePosition());
            m_meshShader.setVec3("uBaseColor", glm::vec3(0.76f, 0.66f, 0.48f));
            m_meshShader.setInt("uMode", m_showNormals ? 1 : 0);

            m_gpuMesh.vao.bind();
            if (m_wireframe)
            {
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                m_meshShader.setInt("uMode", 2);
                glDrawElements(GL_TRIANGLES, m_gpuMesh.indexCount, GL_UNSIGNED_INT, nullptr);
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }
            else
            {
                glDrawElements(GL_TRIANGLES, m_gpuMesh.indexCount, GL_UNSIGNED_INT, nullptr);
            }

            glDisable(GL_CULL_FACE);
        }

        gl::VertexArray::unbind();
        gl::Shader::unbind();
        glDisable(GL_DEPTH_TEST);
        gl::Framebuffer::bindDefault();
    }

    gl::Shader m_meshShader;
    gl::Shader m_lineShader;
    gl::VertexArray m_gridVao;
    gl::Buffer m_gridVbo;
    GLsizei m_gridVertexCount = 0;

    ViewportFbo m_viewportFbo;
    OrbitCamera m_camera;

    std::vector<glm::vec2> m_controlPoints;
    std::vector<modeller::CubicBezierSegment> m_segments;
    std::vector<modeller::ProfileSample> m_samples;
    cg::DatMeshData m_cpuMesh;
    GpuMesh m_gpuMesh;

    int m_radialSegments = 32;
    int m_samplesPerSegment = 10;
    float m_smoothness = 0.5f;
    int m_dragIndex = -1;
    bool m_meshDirty = true;

    bool m_wireframe = false;
    bool m_showNormals = false;
    bool m_backfaceCull = false;
    bool m_showGrid = true;

    char m_exportPathBuf[512]{};
    std::string m_exportStatus;
    bool m_exportStatusOk = true;
};

struct GlfwGuard
{
    bool initialized = false;
    GLFWwindow *window = nullptr;

    ~GlfwGuard()
    {
        if (window != nullptr)
        {
            glfwDestroyWindow(window);
        }
        if (initialized)
        {
            glfwTerminate();
        }
    }
};

struct ImGuiGuard
{
    bool initialized = false;

    ~ImGuiGuard()
    {
        if (initialized)
        {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }
    }
};

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

    glfwSetErrorCallback([](int code, const char *desc)
                         { std::cerr << "[GLFW Error " << code << "] " << desc << '\n'; });

    GlfwGuard glfw;
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW.\n";
        return 1;
    }
    glfw.initialized = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    if (smokeTest)
    {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    glfw.window = glfwCreateWindow(1280, 760, "SOR Modeller (OpenGL 4.6 Core)", nullptr, nullptr);
    if (glfw.window == nullptr)
    {
        std::cerr << "Failed to create OpenGL 4.6 Core GLFW window.\n";
        return 1;
    }
    glfwMakeContextCurrent(glfw.window);
    glfwSwapInterval(smokeTest ? 0 : 1);

    if (!gladLoadGL(glfwGetProcAddress))
    {
        std::cerr << "Failed to initialize GLAD.\n";
        return 1;
    }

    ImGuiGuard imgui;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(glfw.window, true);
    ImGui_ImplOpenGL3_Init("#version 460 core");
    imgui.initialized = true;

    try
    {
        const std::filesystem::path exeDir = getExecutableDirectory(argc > 0 ? argv[0] : nullptr);
        SorModellerApp app(exeDir);

        int frameCount = 0;
        while (!glfwWindowShouldClose(glfw.window))
        {
            glfwPollEvents();

            int winW = 1280;
            int winH = 760;
            glfwGetFramebufferSize(glfw.window, &winW, &winH);

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            if (winW > 0 && winH > 0)
            {
                app.renderFrame(winW, winH);
            }

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(glfw.window);

            ++frameCount;
            if (smokeTest && frameCount >= 120)
            {
                break;
            }
        }

        if (!gl::checkNoErrors("SOR_Modeller"))
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
