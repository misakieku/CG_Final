#include "modeller/sor_mesh.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace modeller
{

namespace
{

[[nodiscard]] float computeSignedProfileArea(std::span<const ProfileSample> samples) noexcept
{
    float area2 = 0.0f;
    for (std::size_t i = 0; i + 1 < samples.size(); ++i)
    {
        const glm::vec2 &a = samples[i].position;
        const glm::vec2 &b = samples[i + 1].position;
        area2 += (a.x + b.x) * (b.y - a.y);
    }
    return 0.5f * area2;
}

} // namespace

cg::DatMeshData buildSorMesh(std::span<const ProfileSample> samples, int radialSegments)
{
    cg::DatMeshData mesh;
    const int segs = std::max(3, radialSegments);
    if (samples.size() < 2)
    {
        return mesh;
    }

    // Positive signed area corresponds to bottom-to-top outer traversal (CCW outward winding).
    const float orientationSign = (computeSignedProfileArea(samples) >= 0.0f) ? 1.0f : -1.0f;
    const bool flipWinding = (orientationSign < 0.0f);

    const std::size_t ringCount = samples.size();
    const std::size_t ringStride = static_cast<std::size_t>(segs);
    mesh.positions.reserve(ringCount * ringStride);
    mesh.normals.reserve(ringCount * ringStride);
    mesh.indices.reserve((ringCount - 1) * ringStride * 6);

    const float twoPi = glm::two_pi<float>();
    for (std::size_t i = 0; i < ringCount; ++i)
    {
        const float r = std::max(0.0f, samples[i].position.x);
        const float y = samples[i].position.y;
        const float dr = samples[i].tangent.x;
        const float dy = samples[i].tangent.y;

        for (int j = 0; j < segs; ++j)
        {
            const float theta = twoPi * static_cast<float>(j) / static_cast<float>(segs);
            const float cosT = std::cos(theta);
            const float sinT = std::sin(theta);

            mesh.positions.emplace_back(r * cosT, y, r * sinT);

            // Analytical normal from dS/dt x dS/dtheta with r(t) factored out:
            // N(t, theta) = s * normalize(y'(t)*cos(theta), -r'(t), y'(t)*sin(theta))
            glm::vec3 n(dy * cosT, -dr, dy * sinT);
            n *= orientationSign;
            const float lenSq = glm::dot(n, n);
            mesh.normals.push_back((lenSq > 1e-12f) ? (n / std::sqrt(lenSq)) : glm::vec3(0.0f, 1.0f, 0.0f));
        }
    }

    constexpr float poleEps = 1e-5f;
    for (std::size_t i = 0; i + 1 < ringCount; ++i)
    {
        const float rA = samples[i].position.x;
        const float rB = samples[i + 1].position.x;
        if (rA <= poleEps && rB <= poleEps)
        {
            continue;
        }

        const std::uint32_t row0 = static_cast<std::uint32_t>(i * ringStride);
        const std::uint32_t row1 = static_cast<std::uint32_t>((i + 1) * ringStride);

        for (int j = 0; j < segs; ++j)
        {
            const std::uint32_t j0 = static_cast<std::uint32_t>(j);
            const std::uint32_t j1 = static_cast<std::uint32_t>((j + 1) % segs);

            const std::uint32_t v00 = row0 + j0;
            const std::uint32_t v01 = row0 + j1;
            const std::uint32_t v10 = row1 + j0;
            const std::uint32_t v11 = row1 + j1;

            auto emitTri = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c)
            {
                if (flipWinding)
                {
                    mesh.indices.push_back(a);
                    mesh.indices.push_back(c);
                    mesh.indices.push_back(b);
                }
                else
                {
                    mesh.indices.push_back(a);
                    mesh.indices.push_back(b);
                    mesh.indices.push_back(c);
                }
            };

            // Skip degenerate triangles whose edge collapses on the Y-axis pole
            if (rB > poleEps)
            {
                emitTri(v00, v10, v11);
            }
            if (rA > poleEps)
            {
                emitTri(v00, v11, v01);
            }
        }
    }

    return mesh;
}

} // namespace modeller
