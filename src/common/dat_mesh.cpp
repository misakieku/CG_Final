#include "common/dat_mesh.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace cg
{

namespace
{

[[nodiscard]] bool nextDataLine(std::ifstream &in, std::string &line)
{
    while (std::getline(in, line))
    {
        const std::size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#')
        {
            continue;
        }
        line.erase(0, first);
        return true;
    }
    return false;
}

[[nodiscard]] std::size_t parseSectionCount(const std::string &line, const char *keyword)
{
    std::istringstream iss(line);
    std::string label;
    char eq = '\0';
    std::size_t count = 0;
    if (!(iss >> label >> eq >> count) || label != keyword || eq != '=')
    {
        throw std::runtime_error(std::string("Expected '") + keyword + " = <count>', got: " + line);
    }
    return count;
}

} // namespace

void recomputeSmoothNormals(DatMeshData &mesh)
{
    mesh.normals.assign(mesh.positions.size(), glm::vec3(0.0f));
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const std::uint32_t i0 = mesh.indices[i];
        const std::uint32_t i1 = mesh.indices[i + 1];
        const std::uint32_t i2 = mesh.indices[i + 2];
        if (i0 >= mesh.positions.size() || i1 >= mesh.positions.size() || i2 >= mesh.positions.size())
        {
            continue;
        }
        const glm::vec3 e1 = mesh.positions[i1] - mesh.positions[i0];
        const glm::vec3 e2 = mesh.positions[i2] - mesh.positions[i0];
        const glm::vec3 areaNormal = glm::cross(e1, e2);
        mesh.normals[i0] += areaNormal;
        mesh.normals[i1] += areaNormal;
        mesh.normals[i2] += areaNormal;
    }

    for (glm::vec3 &n : mesh.normals)
    {
        const float lenSq = glm::dot(n, n);
        n = (lenSq > 1e-12f) ? (n / std::sqrt(lenSq)) : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

bool saveDatMesh(const std::filesystem::path &path, const DatMeshData &mesh, std::string *errorMsg)
{
    if (mesh.positions.empty() || mesh.indices.size() < 3 || (mesh.indices.size() % 3) != 0)
    {
        if (errorMsg != nullptr)
        {
            *errorMsg = "Cannot export empty or invalid triangle mesh.";
        }
        return false;
    }

    std::error_code ec;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    std::FILE *fp = std::fopen(path.string().c_str(), "w");
    if (fp == nullptr)
    {
        if (errorMsg != nullptr)
        {
            *errorMsg = "Failed to open output file: " + path.string();
        }
        return false;
    }

    std::fprintf(fp, "VERTEX = %zu\n", mesh.positions.size());
    for (const glm::vec3 &p : mesh.positions)
    {
        std::fprintf(fp, "%.6f %.6f %.6f\n", p.x, p.y, p.z);
    }

    const std::size_t faceCount = mesh.indices.size() / 3;
    std::fprintf(fp, "FACE = %zu\n", faceCount);
    for (std::size_t i = 0; i < faceCount; ++i)
    {
        std::fprintf(fp, "%u %u %u\n", mesh.indices[i * 3], mesh.indices[i * 3 + 1], mesh.indices[i * 3 + 2]);
    }

    if (mesh.normals.size() == mesh.positions.size())
    {
        std::fprintf(fp, "NORMAL = %zu\n", mesh.normals.size());
        for (const glm::vec3 &n : mesh.normals)
        {
            std::fprintf(fp, "%.6f %.6f %.6f\n", n.x, n.y, n.z);
        }
    }

    const bool writeOk = std::ferror(fp) == 0;
    std::fclose(fp);
    if (!writeOk && errorMsg != nullptr)
    {
        *errorMsg = "I/O error while writing: " + path.string();
    }
    return writeOk;
}

DatMeshData loadDatMesh(const std::filesystem::path &path)
{
    std::ifstream in(path);
    if (!in)
    {
        throw std::runtime_error("Failed to open .dat file: " + path.string());
    }

    DatMeshData mesh;
    std::string line;

    if (!nextDataLine(in, line))
    {
        throw std::runtime_error("Empty .dat file: " + path.string());
    }
    const std::size_t vertexCount = parseSectionCount(line, "VERTEX");
    mesh.positions.reserve(vertexCount);
    for (std::size_t i = 0; i < vertexCount; ++i)
    {
        if (!nextDataLine(in, line))
        {
            throw std::runtime_error("Unexpected EOF while reading VERTEX entries");
        }
        std::istringstream iss(line);
        glm::vec3 p{};
        if (!(iss >> p.x >> p.y >> p.z))
        {
            throw std::runtime_error("Malformed vertex line: " + line);
        }
        mesh.positions.push_back(p);
    }

    if (!nextDataLine(in, line))
    {
        throw std::runtime_error("Missing FACE section in .dat file");
    }
    const std::size_t faceCount = parseSectionCount(line, "FACE");
    mesh.indices.reserve(faceCount * 3);
    for (std::size_t i = 0; i < faceCount; ++i)
    {
        if (!nextDataLine(in, line))
        {
            throw std::runtime_error("Unexpected EOF while reading FACE entries");
        }
        std::istringstream iss(line);
        std::uint32_t i0 = 0;
        std::uint32_t i1 = 0;
        std::uint32_t i2 = 0;
        if (!(iss >> i0 >> i1 >> i2))
        {
            throw std::runtime_error("Malformed face line: " + line);
        }
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
        {
            throw std::runtime_error("Face vertex index out of bounds");
        }
        mesh.indices.push_back(i0);
        mesh.indices.push_back(i1);
        mesh.indices.push_back(i2);
    }

    if (nextDataLine(in, line))
    {
        const std::size_t normalCount = parseSectionCount(line, "NORMAL");
        if (normalCount == vertexCount)
        {
            mesh.normals.reserve(normalCount);
            for (std::size_t i = 0; i < normalCount; ++i)
            {
                if (!nextDataLine(in, line))
                {
                    throw std::runtime_error("Unexpected EOF while reading NORMAL entries");
                }
                std::istringstream iss(line);
                glm::vec3 n{};
                if (!(iss >> n.x >> n.y >> n.z))
                {
                    throw std::runtime_error("Malformed normal line: " + line);
                }
                const float lenSq = glm::dot(n, n);
                mesh.normals.push_back(lenSq > 1e-12f ? (n / std::sqrt(lenSq)) : glm::vec3(0.0f, 1.0f, 0.0f));
            }
            return mesh;
        }
    }

    recomputeSmoothNormals(mesh);
    return mesh;
}

} // namespace cg
