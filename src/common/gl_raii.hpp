#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace gl
{

namespace detail
{

/// Non-copyable, move-only RAII wrapper for a raw OpenGL object handle.
template <typename Deleter>
class UniqueId
{
public:
    UniqueId() = default;
    explicit UniqueId(GLuint id) noexcept : m_id(id)
    {
    }
    ~UniqueId()
    {
        reset();
    }

    UniqueId(const UniqueId &) = delete;
    UniqueId &operator=(const UniqueId &) = delete;

    UniqueId(UniqueId &&other) noexcept : m_id(std::exchange(other.m_id, 0))
    {
    }
    UniqueId &operator=(UniqueId &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_id = std::exchange(other.m_id, 0);
        }
        return *this;
    }

    void reset(GLuint newId = 0) noexcept
    {
        if (m_id != 0)
        {
            Deleter::destroy(m_id);
        }
        m_id = newId;
    }

    [[nodiscard]] GLuint id() const noexcept
    {
        return m_id;
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_id != 0;
    }

private:
    GLuint m_id = 0;
};

struct BufferDeleter
{
    static void destroy(GLuint id) noexcept;
};
struct VertexArrayDeleter
{
    static void destroy(GLuint id) noexcept;
};
struct TextureDeleter
{
    static void destroy(GLuint id) noexcept;
};
struct FramebufferDeleter
{
    static void destroy(GLuint id) noexcept;
};
struct RenderbufferDeleter
{
    static void destroy(GLuint id) noexcept;
};
struct ProgramDeleter
{
    static void destroy(GLuint id) noexcept;
};

} // namespace detail

/// RAII wrapper for an OpenGL buffer object (VBO / EBO / UBO).
class Buffer
{
public:
    Buffer() = default;
    [[nodiscard]] static Buffer create();

    void bind(GLenum target) const noexcept;
    static void unbind(GLenum target) noexcept;

    void allocate(GLenum target, GLsizeiptr byteSize, const void *data, GLenum usage = GL_STATIC_DRAW) const;
    void update(GLenum target, GLintptr offset, GLsizeiptr byteSize, const void *data) const;

    template <typename T>
    void upload(GLenum target, std::span<const T> data, GLenum usage = GL_STATIC_DRAW) const
    {
        allocate(target, static_cast<GLsizeiptr>(data.size_bytes()), data.data(), usage);
    }

    void reset() noexcept
    {
        m_handle.reset();
    }
    [[nodiscard]] GLuint id() const noexcept
    {
        return m_handle.id();
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(m_handle);
    }

private:
    explicit Buffer(GLuint id) noexcept : m_handle(id)
    {
    }
    detail::UniqueId<detail::BufferDeleter> m_handle;
};

/// RAII wrapper for an OpenGL Vertex Array Object (VAO).
class VertexArray
{
public:
    VertexArray() = default;
    [[nodiscard]] static VertexArray create();

    void bind() const noexcept;
    static void unbind() noexcept;

    void setAttribute(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,
                      std::size_t offset) const noexcept;

    void reset() noexcept
    {
        m_handle.reset();
    }
    [[nodiscard]] GLuint id() const noexcept
    {
        return m_handle.id();
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(m_handle);
    }

private:
    explicit VertexArray(GLuint id) noexcept : m_handle(id)
    {
    }
    detail::UniqueId<detail::VertexArrayDeleter> m_handle;
};

/// Parameters describing a 2D texture allocation.
struct Texture2DDesc
{
    GLsizei width = 0;
    GLsizei height = 0;
    GLenum internalFormat = GL_RGBA8;
    GLenum format = GL_RGBA;
    GLenum type = GL_UNSIGNED_BYTE;
    GLenum minFilter = GL_LINEAR;
    GLenum magFilter = GL_LINEAR;
    GLenum wrapS = GL_REPEAT;
    GLenum wrapT = GL_REPEAT;
    bool generateMipmaps = false;
};

/// RAII wrapper for an OpenGL Texture object.
class Texture
{
public:
    Texture() = default;
    [[nodiscard]] static Texture create();

    void bind(GLuint unit = 0, GLenum target = GL_TEXTURE_2D) const noexcept;
    static void unbind(GLenum target = GL_TEXTURE_2D) noexcept;

    void create2D(const Texture2DDesc &desc, const void *pixels = nullptr);
    void setFilters(GLenum minFilter, GLenum magFilter, GLenum target = GL_TEXTURE_2D) const noexcept;
    void setWrap(GLenum wrapS, GLenum wrapT, GLenum target = GL_TEXTURE_2D) const noexcept;
    void setBorderColor(const glm::vec4 &color, GLenum target = GL_TEXTURE_2D) const noexcept;
    void generateMipmaps(GLenum target = GL_TEXTURE_2D) const noexcept;

    void reset() noexcept;
    [[nodiscard]] GLuint id() const noexcept
    {
        return m_handle.id();
    }
    [[nodiscard]] GLsizei width() const noexcept
    {
        return m_width;
    }
    [[nodiscard]] GLsizei height() const noexcept
    {
        return m_height;
    }
    [[nodiscard]] GLenum internalFormat() const noexcept
    {
        return m_internalFormat;
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(m_handle);
    }

private:
    explicit Texture(GLuint id) noexcept : m_handle(id)
    {
    }
    detail::UniqueId<detail::TextureDeleter> m_handle;
    GLsizei m_width = 0;
    GLsizei m_height = 0;
    GLenum m_internalFormat = GL_NONE;
};

/// RAII wrapper for an OpenGL Framebuffer Object and optional owned Renderbuffer.
class Framebuffer
{
public:
    Framebuffer() = default;
    [[nodiscard]] static Framebuffer create();

    void bind(GLenum target = GL_FRAMEBUFFER) const noexcept;
    static void bindDefault(GLenum target = GL_FRAMEBUFFER) noexcept;

    void attachTexture2D(GLenum attachment, const Texture &texture, GLint level = 0) const noexcept;
    void attachDepthRenderbuffer(GLsizei width, GLsizei height, GLenum internalFormat = GL_DEPTH24_STENCIL8);
    void setDrawBuffers(std::span<const GLenum> attachments) const noexcept;
    void setNoColorBuffer() const noexcept;

    [[nodiscard]] bool isComplete() const noexcept;
    void ensureComplete() const;

    void reset() noexcept;
    [[nodiscard]] GLuint id() const noexcept
    {
        return m_handle.id();
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(m_handle);
    }

private:
    explicit Framebuffer(GLuint id) noexcept : m_handle(id)
    {
    }
    detail::UniqueId<detail::FramebufferDeleter> m_handle;
    detail::UniqueId<detail::RenderbufferDeleter> m_rbo;
};

/// RAII wrapper for a compiled and linked OpenGL shader program.
class Shader
{
public:
    Shader() = default;
    [[nodiscard]] static Shader compile(std::string_view vertexSource, std::string_view fragmentSource);
    [[nodiscard]] static Shader compileCompute(std::string_view computeSource);
    [[nodiscard]] static Shader compileFiles(const std::filesystem::path &vertexPath, const std::filesystem::path &fragmentPath, const std::filesystem::path &includeRoot = {});
    [[nodiscard]] static Shader compileComputeFile(const std::filesystem::path &computePath, const std::filesystem::path &includeRoot = {});
    [[nodiscard]] static std::string preprocessSourceFile(const std::filesystem::path &entryPath, const std::filesystem::path &includeRoot = {});

    void use() const noexcept;
    static void unbind() noexcept;

    void setInt(std::string_view name, int value) const;
    void setFloat(std::string_view name, float value) const;
    void setVec2(std::string_view name, const glm::vec2 &value) const;
    void setVec3(std::string_view name, const glm::vec3 &value) const;
    void setVec4(std::string_view name, const glm::vec4 &value) const;
    void setMat3(std::string_view name, const glm::mat3 &value) const;
    void setMat4(std::string_view name, const glm::mat4 &value) const;

    void reset() noexcept;
    [[nodiscard]] GLuint id() const noexcept
    {
        return m_handle.id();
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(m_handle);
    }

private:
    explicit Shader(GLuint id) noexcept : m_handle(id)
    {
    }
    [[nodiscard]] static Shader linkStages(std::span<const GLuint> stageIds);
    [[nodiscard]] GLint uniformLocation(std::string_view name) const;

    detail::UniqueId<detail::ProgramDeleter> m_handle;
    mutable std::unordered_map<std::string, GLint> m_uniformCache;
};

/// Drains glGetError() and returns true if no GL errors were recorded.
[[nodiscard]] bool checkNoErrors(std::string_view context = "") noexcept;

} // namespace gl
