#pragma once

#include "common/gl_raii.hpp"

#include <filesystem>

namespace game
{

struct PBRTextures
{
    gl::Texture albedo;
    gl::Texture orm;
    gl::Texture normal;
    gl::Texture height;
};

[[nodiscard]] PBRTextures generateFloor(const std::filesystem::path &shaderDir, int resolution = 1024);
[[nodiscard]] PBRTextures generateWall(const std::filesystem::path &shaderDir, int resolution = 1024);

} // namespace game
