#include "game/Window.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace game
{

namespace
{

void GLAD_API_PTR glDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *userParam)
{
    (void)source;
    (void)length;
    (void)userParam;
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
    {
        return;
    }

    const char *typeStr = "OTHER";
    switch (type)
    {
        case GL_DEBUG_TYPE_ERROR: typeStr = "ERROR"; break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: typeStr = "DEPRECATED"; break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: typeStr = "UNDEFINED_BEHAVIOR"; break;
        case GL_DEBUG_TYPE_PORTABILITY: typeStr = "PORTABILITY"; break;
        case GL_DEBUG_TYPE_PERFORMANCE: typeStr = "PERFORMANCE"; break;
        default: break;
    }
    std::cerr << "[GL Debug " << typeStr << " #" << id << "] " << message << '\n';
}

} // namespace

Window::Window(const WindowConfig &config)
{
    glfwSetErrorCallback([](int code, const char *desc)
                         { std::cerr << "[GLFW Error " << code << "] " << desc << '\n'; });

    if (!glfwInit())
    {
        throw std::runtime_error("Failed to initialize GLFW.");
    }
    m_glfwInitialized = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    if (config.hidden)
    {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }
    if (config.debugContext)
    {
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
    }

    const std::string titleStr(config.title);
    m_window = glfwCreateWindow(config.width, config.height, titleStr.c_str(), nullptr, nullptr);
    if (m_window == nullptr)
    {
        glfwTerminate();
        m_glfwInitialized = false;
        throw std::runtime_error("Failed to create OpenGL 4.6 Core GLFW window.");
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(config.vsync ? 1 : 0);

    if (!gladLoadGL(glfwGetProcAddress))
    {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
        glfwTerminate();
        m_glfwInitialized = false;
        throw std::runtime_error("Failed to load OpenGL 4.6 functions via GLAD.");
    }

    if (config.debugContext)
    {
        GLint flags = 0;
        glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
        if ((flags & GL_CONTEXT_FLAG_DEBUG_BIT) != 0)
        {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(glDebugCallback, nullptr);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
        }
    }

    glfwGetFramebufferSize(m_window, &m_width, &m_height);
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, &Window::onFramebufferResize);
    glfwSetKeyCallback(m_window, &Window::onKey);
    glfwSetCursorPosCallback(m_window, &Window::onCursorPos);
    glfwSetScrollCallback(m_window, &Window::onScroll);

    m_lastFrameTime = glfwGetTime();
}

Window::~Window()
{
    if (m_window != nullptr)
    {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    if (m_glfwInitialized)
    {
        glfwTerminate();
        m_glfwInitialized = false;
    }
}

void Window::pollEvents() noexcept
{
    m_prevKeys = m_currKeys;
    m_pendingMouseDelta = glm::vec2(0.0f);
    m_pendingScroll = 0.0f;

    glfwPollEvents();

    m_mouseDelta = m_pendingMouseDelta;
    m_scrollDelta = m_pendingScroll;

    const double now = glfwGetTime();
    m_deltaTime = static_cast<float>(std::clamp(now - m_lastFrameTime, 0.0, 0.1));
    m_lastFrameTime = now;
    m_elapsedTime = static_cast<float>(now);
}

void Window::swapBuffers() noexcept
{
    glfwSwapBuffers(m_window);
}

bool Window::shouldClose() const noexcept
{
    return glfwWindowShouldClose(m_window) != 0;
}

void Window::requestClose() noexcept
{
    glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

float Window::aspectRatio() const noexcept
{
    return (m_height > 0) ? (static_cast<float>(m_width) / static_cast<float>(m_height)) : 1.0f;
}

bool Window::consumeResized() noexcept
{
    const bool wasResized = m_resized;
    m_resized = false;
    return wasResized;
}

void Window::setCursorCaptured(bool captured) noexcept
{
    if (m_cursorCaptured == captured)
    {
        return;
    }
    m_cursorCaptured = captured;
    m_firstMouse = true;
    m_pendingMouseDelta = glm::vec2(0.0f);

    glfwSetInputMode(m_window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported() == GLFW_TRUE)
    {
        glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
    }
}

bool Window::isKeyDown(int glfwKey) const noexcept
{
    if (glfwKey < 0 || glfwKey > GLFW_KEY_LAST)
    {
        return false;
    }
    return m_currKeys[static_cast<std::size_t>(glfwKey)] != 0;
}

bool Window::wasKeyPressed(int glfwKey) const noexcept
{
    if (glfwKey < 0 || glfwKey > GLFW_KEY_LAST)
    {
        return false;
    }
    const auto idx = static_cast<std::size_t>(glfwKey);
    return m_currKeys[idx] != 0 && m_prevKeys[idx] == 0;
}

bool Window::isMouseButtonDown(int glfwButton) const noexcept
{
    return glfwGetMouseButton(m_window, glfwButton) == GLFW_PRESS;
}

void Window::onFramebufferResize(GLFWwindow *win, int width, int height) noexcept
{
    if (auto *self = static_cast<Window *>(glfwGetWindowUserPointer(win)))
    {
        self->m_width = std::max(0, width);
        self->m_height = std::max(0, height);
        self->m_resized = true;
    }
}

void Window::onKey(GLFWwindow *win, int key, int scancode, int action, int mods) noexcept
{
    (void)scancode;
    (void)mods;
    if (key < 0 || key > GLFW_KEY_LAST)
    {
        return;
    }
    if (auto *self = static_cast<Window *>(glfwGetWindowUserPointer(win)))
    {
        self->m_currKeys[static_cast<std::size_t>(key)] = (action != GLFW_RELEASE) ? 1 : 0;
    }
}

void Window::onCursorPos(GLFWwindow *win, double xpos, double ypos) noexcept
{
    auto *self = static_cast<Window *>(glfwGetWindowUserPointer(win));
    if (self == nullptr)
    {
        return;
    }

    const glm::vec2 current(static_cast<float>(xpos), static_cast<float>(ypos));
    if (self->m_firstMouse)
    {
        self->m_lastMousePos = current;
        self->m_firstMouse = false;
        return;
    }

    self->m_pendingMouseDelta += (current - self->m_lastMousePos);
    self->m_lastMousePos = current;
}

void Window::onScroll(GLFWwindow *win, double xoffset, double yoffset) noexcept
{
    (void)xoffset;
    if (auto *self = static_cast<Window *>(glfwGetWindowUserPointer(win)))
    {
        self->m_pendingScroll += static_cast<float>(yoffset);
    }
}

} // namespace game
