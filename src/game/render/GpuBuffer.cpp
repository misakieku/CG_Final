#include "game/render/GpuBuffer.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace game::render
{

GpuBuffer::GpuBuffer(GLuint id, std::size_t sizeBytes, void *mappedPtr) noexcept
    : m_id(id), m_sizeBytes(sizeBytes), m_mappedPtr(mappedPtr)
{
}

GpuBuffer::~GpuBuffer()
{
    reset();
}

GpuBuffer::GpuBuffer(GpuBuffer &&other) noexcept
    : m_id(std::exchange(other.m_id, 0)),
      m_sizeBytes(std::exchange(other.m_sizeBytes, 0)),
      m_mappedPtr(std::exchange(other.m_mappedPtr, nullptr))
{
}

GpuBuffer &GpuBuffer::operator=(GpuBuffer &&other) noexcept
{
    if (this != &other)
    {
        reset();
        m_id = std::exchange(other.m_id, 0);
        m_sizeBytes = std::exchange(other.m_sizeBytes, 0);
        m_mappedPtr = std::exchange(other.m_mappedPtr, nullptr);
    }
    return *this;
}

GpuBuffer GpuBuffer::createImmutable(std::size_t byteSize, const void *initialData, GLbitfield flags)
{
    const std::size_t allocSize = std::max<std::size_t>(16, byteSize);
    GLuint id = 0;
    glCreateBuffers(1, &id);
    if (id == 0)
    {
        throw std::runtime_error("glCreateBuffers failed");
    }
    glNamedBufferStorage(id, static_cast<GLsizeiptr>(allocSize), (byteSize > 0 ? initialData : nullptr), flags);
    return GpuBuffer(id, allocSize);
}

GpuBuffer GpuBuffer::createDynamic(std::size_t byteSize, const void *initialData)
{
    return createImmutable(byteSize, initialData, GL_DYNAMIC_STORAGE_BIT);
}

GpuBuffer GpuBuffer::createPersistentMapped(std::size_t byteSize)
{
    const std::size_t allocSize = std::max<std::size_t>(16, byteSize);
    constexpr GLbitfield storageFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    GLuint id = 0;
    glCreateBuffers(1, &id);
    if (id == 0)
    {
        throw std::runtime_error("glCreateBuffers failed");
    }
    glNamedBufferStorage(id, static_cast<GLsizeiptr>(allocSize), nullptr, storageFlags);
    void *ptr = glMapNamedBufferRange(id, 0, static_cast<GLsizeiptr>(allocSize), storageFlags);
    if (ptr == nullptr)
    {
        glDeleteBuffers(1, &id);
        throw std::runtime_error("glMapNamedBufferRange failed");
    }
    return GpuBuffer(id, allocSize, ptr);
}

void GpuBuffer::upload(std::size_t offsetBytes, std::size_t byteSize, const void *data) const
{
    if (byteSize == 0 || data == nullptr)
    {
        return;
    }
    glNamedBufferSubData(m_id, static_cast<GLintptr>(offsetBytes), static_cast<GLsizeiptr>(byteSize), data);
}

void GpuBuffer::copyFrom(const GpuBuffer &src, std::size_t srcOffset, std::size_t dstOffset, std::size_t byteSize) const
{
    if (byteSize == 0)
    {
        return;
    }
    glCopyNamedBufferSubData(src.m_id, m_id, static_cast<GLintptr>(srcOffset), static_cast<GLintptr>(dstOffset), static_cast<GLsizeiptr>(byteSize));
}

void GpuBuffer::clearUint32(std::uint32_t value) const
{
    glClearNamedBufferData(m_id, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &value);
}

void GpuBuffer::bindBase(GLenum target, GLuint slot) const noexcept
{
    glBindBufferBase(target, slot, m_id);
}

void GpuBuffer::bind(GLenum target) const noexcept
{
    glBindBuffer(target, m_id);
}

void GpuBuffer::reset() noexcept
{
    if (m_id != 0)
    {
        if (m_mappedPtr != nullptr)
        {
            glUnmapNamedBuffer(m_id);
            m_mappedPtr = nullptr;
        }
        glDeleteBuffers(1, &m_id);
        m_id = 0;
    }
    m_sizeBytes = 0;
}

} // namespace game::render
