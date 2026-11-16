#pragma once

#include <glad/gl.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace game::render
{

/// OpenGL 4.5+ DSA buffer wrapper (supports immutable, dynamic subdata, and persistent coherent mapping).
class GpuBuffer
{
public:
    GpuBuffer() = default;
    ~GpuBuffer();

    GpuBuffer(const GpuBuffer &) = delete;
    GpuBuffer &operator=(const GpuBuffer &) = delete;
    GpuBuffer(GpuBuffer &&other) noexcept;
    GpuBuffer &operator=(GpuBuffer &&other) noexcept;

    /// Allocates an immutable GPU-only buffer (optional initial upload).
    [[nodiscard]] static GpuBuffer createImmutable(std::size_t byteSize, const void *initialData = nullptr, GLbitfield flags = 0);

    /// Allocates a buffer allowing glNamedBufferSubData updates.
    [[nodiscard]] static GpuBuffer createDynamic(std::size_t byteSize, const void *initialData = nullptr);

    /// Allocates a persistently mapped, GPU/CPU coherent write buffer (for UBOs / per-frame streaming).
    [[nodiscard]] static GpuBuffer createPersistentMapped(std::size_t byteSize);

    template <typename T>
    [[nodiscard]] static GpuBuffer createImmutableFromSpan(std::span<const T> data, GLbitfield flags = 0)
    {
        return createImmutable(data.size_bytes(), data.data(), flags);
    }

    void upload(std::size_t offsetBytes, std::size_t byteSize, const void *data) const;
    void copyFrom(const GpuBuffer &src, std::size_t srcOffset, std::size_t dstOffset, std::size_t byteSize) const;
    void clearUint32(std::uint32_t value = 0) const;

    void bindBase(GLenum target, GLuint slot) const noexcept;
    void bind(GLenum target) const noexcept;
    void reset() noexcept;

    [[nodiscard]] GLuint id() const noexcept { return m_id; }
    [[nodiscard]] std::size_t sizeBytes() const noexcept { return m_sizeBytes; }
    [[nodiscard]] void *mappedPtr() const noexcept { return m_mappedPtr; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_id != 0; }

private:
    explicit GpuBuffer(GLuint id, std::size_t sizeBytes, void *mappedPtr = nullptr) noexcept;

    GLuint m_id = 0;
    std::size_t m_sizeBytes = 0;
    void *m_mappedPtr = nullptr;
};

} // namespace game::render
