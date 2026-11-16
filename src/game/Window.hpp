#pragma once

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <array>
#include <string_view>

namespace game
{

struct WindowConfig
{
    int width = 1280;
    int height = 720;
    std::string_view title = "MazeGame (OpenGL 4.6 Core)";
    bool vsync = true;
    bool hidden = false;
    bool debugContext = false;
};

/// RAII GLFW window + OpenGL 4.6 Core context with frame-based input & resize tracking.
class Window
{
public:
    explicit Window(const WindowConfig &config = {});
    ~Window();

    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;
    Window(Window &&) = delete;
    Window &operator=(Window &&) = delete;

    void pollEvents() noexcept;
    void swapBuffers() noexcept;

    [[nodiscard]] bool shouldClose() const noexcept;
    void requestClose() noexcept;

    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }
    [[nodiscard]] float aspectRatio() const noexcept;
    [[nodiscard]] bool consumeResized() noexcept;

    void setCursorCaptured(bool captured) noexcept;
    [[nodiscard]] bool isCursorCaptured() const noexcept { return m_cursorCaptured; }

    [[nodiscard]] bool isKeyDown(int glfwKey) const noexcept;
    [[nodiscard]] bool wasKeyPressed(int glfwKey) const noexcept;
    [[nodiscard]] bool isMouseButtonDown(int glfwButton) const noexcept;
    [[nodiscard]] glm::vec2 mouseDelta() const noexcept { return m_mouseDelta; }
    [[nodiscard]] float scrollDelta() const noexcept { return m_scrollDelta; }

    [[nodiscard]] float deltaTime() const noexcept { return m_deltaTime; }
    [[nodiscard]] float elapsedTime() const noexcept { return m_elapsedTime; }
    [[nodiscard]] GLFWwindow *nativeHandle() const noexcept { return m_window; }

private:
    static void onFramebufferResize(GLFWwindow *win, int width, int height) noexcept;
    static void onKey(GLFWwindow *win, int key, int scancode, int action, int mods) noexcept;
    static void onCursorPos(GLFWwindow *win, double xpos, double ypos) noexcept;
    static void onScroll(GLFWwindow *win, double xoffset, double yoffset) noexcept;

    GLFWwindow *m_window = nullptr;
    bool m_glfwInitialized = false;

    int m_width = 0;
    int m_height = 0;
    bool m_resized = false;

    bool m_cursorCaptured = false;
    bool m_firstMouse = true;
    glm::vec2 m_lastMousePos{0.0f};
    glm::vec2 m_pendingMouseDelta{0.0f};
    glm::vec2 m_mouseDelta{0.0f};
    float m_pendingScroll = 0.0f;
    float m_scrollDelta = 0.0f;

    std::array<unsigned char, GLFW_KEY_LAST + 1> m_currKeys{};
    std::array<unsigned char, GLFW_KEY_LAST + 1> m_prevKeys{};

    double m_lastFrameTime = 0.0;
    float m_deltaTime = 0.0f;
    float m_elapsedTime = 0.0f;
};

} // namespace game
