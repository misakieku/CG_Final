#include "common/gl_raii.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace gl
{

namespace
{

struct ShaderStageDeleter
{
    static void destroy(GLuint id) noexcept
    {
        glDeleteShader(id);
    }
};

[[nodiscard]] detail::UniqueId<ShaderStageDeleter> compileStage(GLenum type, std::string_view source)
{
    GLuint stageId = glCreateShader(type);
    if (stageId == 0)
    {
        throw std::runtime_error("glCreateShader failed");
    }
    detail::UniqueId<ShaderStageDeleter> stage(stageId);

    const char *srcPtr = source.data();
    const auto srcLen = static_cast<GLint>(source.size());
    glShaderSource(stage.id(), 1, &srcPtr, &srcLen);
    glCompileShader(stage.id());

    GLint compiled = GL_FALSE;
    glGetShaderiv(stage.id(), GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE)
    {
        return stage;
    }

    GLint logLength = 0;
    glGetShaderiv(stage.id(), GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(logLength > 0 ? logLength : 1), '\0');
    if (logLength > 0)
    {
        glGetShaderInfoLog(stage.id(), logLength, nullptr, log.data());
    }
    const char *stageName = "Unknown";
    if (type == GL_VERTEX_SHADER)
    {
        stageName = "Vertex";
    }
    else if (type == GL_FRAGMENT_SHADER)
    {
        stageName = "Fragment";
    }
    else if (type == GL_COMPUTE_SHADER)
    {
        stageName = "Compute";
    }
    throw std::runtime_error(std::string(stageName) + " shader compile error:\n" + log);
}

[[nodiscard]] const char *framebufferStatusToString(GLenum status) noexcept
{
    switch (status)
    {
    case GL_FRAMEBUFFER_COMPLETE:
        return "GL_FRAMEBUFFER_COMPLETE";
    case GL_FRAMEBUFFER_UNDEFINED:
        return "GL_FRAMEBUFFER_UNDEFINED";
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
        return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
        return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
    case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
        return "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER";
    case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
        return "GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER";
    case GL_FRAMEBUFFER_UNSUPPORTED:
        return "GL_FRAMEBUFFER_UNSUPPORTED";
    case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
        return "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
    default:
        return "UNKNOWN_FRAMEBUFFER_STATUS";
    }
}

} // namespace

namespace detail
{

void BufferDeleter::destroy(GLuint id) noexcept
{
    glDeleteBuffers(1, &id);
}
void VertexArrayDeleter::destroy(GLuint id) noexcept
{
    glDeleteVertexArrays(1, &id);
}
void TextureDeleter::destroy(GLuint id) noexcept
{
    glDeleteTextures(1, &id);
}
void FramebufferDeleter::destroy(GLuint id) noexcept
{
    glDeleteFramebuffers(1, &id);
}
void RenderbufferDeleter::destroy(GLuint id) noexcept
{
    glDeleteRenderbuffers(1, &id);
}
void ProgramDeleter::destroy(GLuint id) noexcept
{
    glDeleteProgram(id);
}

} // namespace detail

// --- Buffer ---

Buffer Buffer::create()
{
    GLuint id = 0;
    glGenBuffers(1, &id);
    if (id == 0)
    {
        throw std::runtime_error("glGenBuffers failed");
    }
    return Buffer(id);
}

void Buffer::bind(GLenum target) const noexcept
{
    glBindBuffer(target, id());
}

void Buffer::unbind(GLenum target) noexcept
{
    glBindBuffer(target, 0);
}

void Buffer::allocate(GLenum target, GLsizeiptr byteSize, const void *data, GLenum usage) const
{
    bind(target);
    glBufferData(target, byteSize, data, usage);
}

void Buffer::update(GLenum target, GLintptr offset, GLsizeiptr byteSize, const void *data) const
{
    bind(target);
    glBufferSubData(target, offset, byteSize, data);
}

// --- VertexArray ---

VertexArray VertexArray::create()
{
    GLuint id = 0;
    glGenVertexArrays(1, &id);
    if (id == 0)
    {
        throw std::runtime_error("glGenVertexArrays failed");
    }
    return VertexArray(id);
}

void VertexArray::bind() const noexcept
{
    glBindVertexArray(id());
}

void VertexArray::unbind() noexcept
{
    glBindVertexArray(0);
}

void VertexArray::setAttribute(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,
                               std::size_t offset) const noexcept
{
    bind();
    glEnableVertexAttribArray(index);
    glVertexAttribPointer(index, size, type, normalized, stride, reinterpret_cast<const void *>(offset));
}

// --- Texture ---

Texture Texture::create()
{
    GLuint id = 0;
    glGenTextures(1, &id);
    if (id == 0)
    {
        throw std::runtime_error("glGenTextures failed");
    }
    return Texture(id);
}

void Texture::bind(GLuint unit, GLenum target) const noexcept
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(target, id());
}

void Texture::unbind(GLenum target) noexcept
{
    glBindTexture(target, 0);
}

void Texture::create2D(const Texture2DDesc &desc, const void *pixels)
{
    if (!*this)
    {
        *this = Texture::create();
    }
    glBindTexture(GL_TEXTURE_2D, id());

    // Ensure 1-byte or 3-byte formats (like R8/RGB8) upload without 4-byte row padding issues
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(desc.internalFormat), desc.width, desc.height, 0, desc.format,
                 desc.type, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    setFilters(desc.minFilter, desc.magFilter, GL_TEXTURE_2D);
    setWrap(desc.wrapS, desc.wrapT, GL_TEXTURE_2D);
    if (desc.generateMipmaps)
    {
        generateMipmaps(GL_TEXTURE_2D);
    }

    m_width = desc.width;
    m_height = desc.height;
    m_internalFormat = desc.internalFormat;
}

void Texture::setFilters(GLenum minFilter, GLenum magFilter, GLenum target) const noexcept
{
    glBindTexture(target, id());
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(minFilter));
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(magFilter));
}

void Texture::setWrap(GLenum wrapS, GLenum wrapT, GLenum target) const noexcept
{
    glBindTexture(target, id());
    glTexParameteri(target, GL_TEXTURE_WRAP_S, static_cast<GLint>(wrapS));
    glTexParameteri(target, GL_TEXTURE_WRAP_T, static_cast<GLint>(wrapT));
}

void Texture::setBorderColor(const glm::vec4 &color, GLenum target) const noexcept
{
    glBindTexture(target, id());
    glTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, glm::value_ptr(color));
}

void Texture::generateMipmaps(GLenum target) const noexcept
{
    glBindTexture(target, id());
    glGenerateMipmap(target);
}

void Texture::reset() noexcept
{
    m_handle.reset();
    m_width = 0;
    m_height = 0;
    m_internalFormat = GL_NONE;
}

// --- Framebuffer ---

Framebuffer Framebuffer::create()
{
    GLuint id = 0;
    glGenFramebuffers(1, &id);
    if (id == 0)
    {
        throw std::runtime_error("glGenFramebuffers failed");
    }
    return Framebuffer(id);
}

void Framebuffer::bind(GLenum target) const noexcept
{
    glBindFramebuffer(target, id());
}

void Framebuffer::bindDefault(GLenum target) noexcept
{
    glBindFramebuffer(target, 0);
}

void Framebuffer::attachTexture2D(GLenum attachment, const Texture &texture, GLint level) const noexcept
{
    bind(GL_FRAMEBUFFER);
    glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, texture.id(), level);
}

void Framebuffer::attachDepthRenderbuffer(GLsizei width, GLsizei height, GLenum internalFormat)
{
    if (!m_rbo)
    {
        GLuint rboId = 0;
        glGenRenderbuffers(1, &rboId);
        if (rboId == 0)
        {
            throw std::runtime_error("glGenRenderbuffers failed");
        }
        m_rbo.reset(rboId);
    }

    bind(GL_FRAMEBUFFER);
    glBindRenderbuffer(GL_RENDERBUFFER, m_rbo.id());
    glRenderbufferStorage(GL_RENDERBUFFER, internalFormat, width, height);

    const GLenum attachment = (internalFormat == GL_DEPTH24_STENCIL8 || internalFormat == GL_DEPTH32F_STENCIL8)
                                  ? GL_DEPTH_STENCIL_ATTACHMENT
                                  : GL_DEPTH_ATTACHMENT;
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, m_rbo.id());
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void Framebuffer::setDrawBuffers(std::span<const GLenum> attachments) const noexcept
{
    bind(GL_FRAMEBUFFER);
    glDrawBuffers(static_cast<GLsizei>(attachments.size()), attachments.data());
}

void Framebuffer::setNoColorBuffer() const noexcept
{
    bind(GL_FRAMEBUFFER);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
}

bool Framebuffer::isComplete() const noexcept
{
    bind(GL_FRAMEBUFFER);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

void Framebuffer::ensureComplete() const
{
    bind(GL_FRAMEBUFFER);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        throw std::runtime_error(std::string("Incomplete framebuffer: ") + framebufferStatusToString(status));
    }
}

void Framebuffer::reset() noexcept
{
    m_rbo.reset();
    m_handle.reset();
}

// --- Shader ---

Shader Shader::linkStages(std::span<const GLuint> stageIds)
{
    GLuint programId = glCreateProgram();
    if (programId == 0)
    {
        throw std::runtime_error("glCreateProgram failed");
    }
    Shader shader(programId);

    for (GLuint sid : stageIds)
    {
        glAttachShader(shader.id(), sid);
    }
    glLinkProgram(shader.id());
    for (GLuint sid : stageIds)
    {
        glDetachShader(shader.id(), sid);
    }

    GLint linked = GL_FALSE;
    glGetProgramiv(shader.id(), GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE)
    {
        return shader;
    }

    GLint logLength = 0;
    glGetProgramiv(shader.id(), GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(logLength > 0 ? logLength : 1), '\0');
    if (logLength > 0)
    {
        glGetProgramInfoLog(shader.id(), logLength, nullptr, log.data());
    }
    throw std::runtime_error("Shader link error:\n" + log);
}

Shader Shader::compile(std::string_view vertexSource, std::string_view fragmentSource)
{
    auto vert = compileStage(GL_VERTEX_SHADER, vertexSource);
    auto frag = compileStage(GL_FRAGMENT_SHADER, fragmentSource);
    const GLuint stages[] = {vert.id(), frag.id()};
    return linkStages(stages);
}

Shader Shader::compileCompute(std::string_view computeSource)
{
    auto comp = compileStage(GL_COMPUTE_SHADER, computeSource);
    const GLuint stages[] = {comp.id()};
    return linkStages(stages);
}

namespace
{

[[nodiscard]] std::string readAndPreprocessRecursive(const std::filesystem::path &filePath, const std::filesystem::path &includeRoot, std::unordered_set<std::string> &pragmaOnceFiles, int depth)
{
    if (depth > 32)
    {
        throw std::runtime_error("GLSL #include recursion limit exceeded at: " + filePath.generic_string());
    }

    const std::string canonicalKey = std::filesystem::weakly_canonical(filePath).generic_string();
    if (pragmaOnceFiles.contains(canonicalKey))
    {
        return {};
    }

    std::ifstream in(filePath, std::ios::binary);
    if (!in)
    {
        throw std::runtime_error("Failed to open GLSL shader file: " + filePath.generic_string());
    }

    std::ostringstream out;
    std::string line;
    bool firstLine = true;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (firstLine && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF)
        {
            line.erase(0, 3);
        }
        firstLine = false;

        const std::size_t firstNonWs = line.find_first_not_of(" \t");
        if (firstNonWs != std::string::npos)
        {
            const std::string_view trimmed(line.data() + firstNonWs, line.size() - firstNonWs);
            if (trimmed.starts_with("#pragma once"))
            {
                pragmaOnceFiles.insert(canonicalKey);
                continue;
            }
            if (trimmed.starts_with("#include"))
            {
                const std::size_t q0 = trimmed.find('"');
                const std::size_t q1 = (q0 != std::string_view::npos) ? trimmed.find('"', q0 + 1) : std::string_view::npos;
                if (q0 == std::string_view::npos || q1 == std::string_view::npos)
                {
                    throw std::runtime_error("Malformed #include in " + filePath.generic_string() + ": " + line);
                }
                const std::filesystem::path rel(trimmed.substr(q0 + 1, q1 - q0 - 1));
                std::filesystem::path resolved = filePath.parent_path() / rel;
                if (!std::filesystem::exists(resolved) && !includeRoot.empty())
                {
                    resolved = includeRoot / rel;
                }
                out << "// [begin " << rel.generic_string() << "]\n";
                out << readAndPreprocessRecursive(resolved, includeRoot, pragmaOnceFiles, depth + 1);
                out << "// [end " << rel.generic_string() << "]\n";
                continue;
            }
        }
        out << line << '\n';
    }
    return out.str();
}

} // namespace

std::string Shader::preprocessSourceFile(const std::filesystem::path &entryPath, const std::filesystem::path &includeRoot)
{
    std::unordered_set<std::string> pragmaOnceFiles;
    const std::filesystem::path root = includeRoot.empty() ? entryPath.parent_path() : includeRoot;
    return readAndPreprocessRecursive(entryPath, root, pragmaOnceFiles, 0);
}

Shader Shader::compileFiles(const std::filesystem::path &vertexPath, const std::filesystem::path &fragmentPath, const std::filesystem::path &includeRoot)
{
    const std::string vertSrc = preprocessSourceFile(vertexPath, includeRoot);
    const std::string fragSrc = preprocessSourceFile(fragmentPath, includeRoot);
    try
    {
        return compile(vertSrc, fragSrc);
    }
    catch (const std::exception &ex)
    {
        throw std::runtime_error("Shader [" + vertexPath.filename().generic_string() + " + " + fragmentPath.filename().generic_string() + "]: " + ex.what());
    }
}

Shader Shader::compileComputeFile(const std::filesystem::path &computePath, const std::filesystem::path &includeRoot)
{
    const std::string compSrc = preprocessSourceFile(computePath, includeRoot);
    try
    {
        return compileCompute(compSrc);
    }
    catch (const std::exception &ex)
    {
        throw std::runtime_error("Compute Shader [" + computePath.filename().generic_string() + "]: " + ex.what());
    }
}

void Shader::use() const noexcept
{
    glUseProgram(id());
}

void Shader::unbind() noexcept
{
    glUseProgram(0);
}

GLint Shader::uniformLocation(std::string_view name) const
{
    std::string key(name);
    if (const auto it = m_uniformCache.find(key); it != m_uniformCache.end())
    {
        return it->second;
    }
    const GLint loc = glGetUniformLocation(id(), key.c_str());
    m_uniformCache.emplace(std::move(key), loc);
    return loc;
}

void Shader::setInt(std::string_view name, int value) const
{
    glUniform1i(uniformLocation(name), value);
}

void Shader::setFloat(std::string_view name, float value) const
{
    glUniform1f(uniformLocation(name), value);
}

void Shader::setVec2(std::string_view name, const glm::vec2 &value) const
{
    glUniform2fv(uniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setVec3(std::string_view name, const glm::vec3 &value) const
{
    glUniform3fv(uniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setVec4(std::string_view name, const glm::vec4 &value) const
{
    glUniform4fv(uniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setMat3(std::string_view name, const glm::mat3 &value) const
{
    glUniformMatrix3fv(uniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::setMat4(std::string_view name, const glm::mat4 &value) const
{
    glUniformMatrix4fv(uniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::reset() noexcept
{
    m_uniformCache.clear();
    m_handle.reset();
}

// --- Error check helper ---

bool checkNoErrors(std::string_view context) noexcept
{
    bool clean = true;
    for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError())
    {
        clean = false;
        std::cerr << "[GL Error";
        if (!context.empty())
        {
            std::cerr << " @ " << context;
        }
        std::cerr << "] 0x" << std::hex << err << std::dec << '\n';
    }
    return clean;
}

} // namespace gl
