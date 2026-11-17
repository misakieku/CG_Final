#include "game/TextureGenerator.hpp"

#include <glad/gl.h>

#include <algorithm>

namespace game
{

namespace
{

[[nodiscard]] gl::Texture allocateBakedTexture(int resolution, GLenum internalFormat, GLenum format, GLenum type, bool mipmapped)
{
    gl::Texture tex;
    tex.create2D(
        gl::Texture2DDesc{
            .width = resolution,
            .height = resolution,
            .internalFormat = internalFormat,
            .format = format,
            .type = type,
            .minFilter = static_cast<GLenum>(mipmapped ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR),
            .magFilter = GL_LINEAR,
            .wrapS = GL_REPEAT,
            .wrapT = GL_REPEAT,
            .generateMipmaps = mipmapped,
        },
        nullptr);
    return tex;
}

} // namespace

PBRTextures generateFloor(const std::filesystem::path &shaderDir, int resolution)
{
    const int size = std::max(64, resolution);
    const GLuint groups = static_cast<GLuint>((size + 15) / 16);

    PBRTextures out;
    out.albedo = allocateBakedTexture(size, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, true);
    out.orm = allocateBakedTexture(size, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, true);
    out.normal = allocateBakedTexture(size, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, true);
    out.height = allocateBakedTexture(size, GL_R16F, GL_RED, GL_FLOAT, false);

    // Pass 1: Procedural floor albedo, ORM, and height
    const gl::Shader floorShader = gl::Shader::compileComputeFile(shaderDir / "tg/floor.comp", shaderDir);
    floorShader.use();
    glBindImageTexture(0, out.albedo.id(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glBindImageTexture(1, out.orm.id(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glBindImageTexture(2, out.height.id(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);
    glDispatchCompute(groups, groups, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // Pass 2: Seamless 3x3 Sobel height -> tangent-space normal
    const gl::Shader normalShader = gl::Shader::compileComputeFile(shaderDir / "tg/height_to_normal.comp", shaderDir);
    normalShader.use();
    normalShader.setInt("uHeightTex", 0);
    normalShader.setFloat("uStrength", 24.0f);
    normalShader.setFloat("uMaxHeight", 0.055f);
    glBindTextureUnit(0, out.height.id());
    glBindImageTexture(0, out.normal.id(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glDispatchCompute(groups, groups, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    gl::Shader::unbind();
    out.albedo.generateMipmaps();
    out.orm.generateMipmaps();
    out.normal.generateMipmaps();
    out.height.generateMipmaps();

    return out;
}

} // namespace game
