#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cg
{

/// Mesh data matching the course .dat format (positions, triangle indices, and optional analytical normals).
struct DatMeshData
{
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<std::uint32_t> indices;
};

/// Saves mesh data to .dat (VERTEX + FACE + trailing NORMAL when present).
bool saveDatMesh(const std::filesystem::path &path, const DatMeshData &mesh, std::string *errorMsg = nullptr);

/// Loads .dat mesh; reconstructs area-weighted smooth normals if NORMAL section is absent.
[[nodiscard]] DatMeshData loadDatMesh(const std::filesystem::path &path);

/// Recomputes area-weighted smooth vertex normals from triangle geometry.
void recomputeSmoothNormals(DatMeshData &mesh);

} // namespace cg
