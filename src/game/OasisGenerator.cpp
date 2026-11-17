#include "game/OasisGenerator.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <vector>

namespace game
{

namespace
{

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.2831853f;

struct MeshBuilder
{
    std::vector<render::SceneVertex> vertices;
    std::vector<std::uint32_t> indices;

    void addTriangleOriented(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2, glm::vec3 hintNormal)
    {
        glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
        const float lenSq = glm::dot(n, n);
        if (lenSq < 1e-10f)
        {
            return;
        }
        n /= std::sqrt(lenSq);
        if (glm::dot(n, hintNormal) < 0.0f)
        {
            std::swap(p1, p2);
            std::swap(uv1, uv2);
            n = -n;
        }

        const auto base = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back({p0, n, uv0});
        vertices.push_back({p1, n, uv1});
        vertices.push_back({p2, n, uv2});
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
    }

    void addDoubleSidedTri(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2)
    {
        glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
        const float lenSq = glm::dot(n, n);
        if (lenSq < 1e-10f)
        {
            return;
        }
        n /= std::sqrt(lenSq);

        const auto base = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back({p0, n, uv0});
        vertices.push_back({p1, n, uv1});
        vertices.push_back({p2, n, uv2});
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);

        const auto backBase = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back({p0, -n, uv0});
        vertices.push_back({p2, -n, uv2});
        vertices.push_back({p1, -n, uv1});
        indices.push_back(backBase + 0);
        indices.push_back(backBase + 1);
        indices.push_back(backBase + 2);
    }

    void addDoubleSidedQuad(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2, glm::vec2 uv3)
    {
        addDoubleSidedTri(p0, p1, p2, uv0, uv1, uv2);
        addDoubleSidedTri(p0, p2, p3, uv0, uv2, uv3);
    }

    void recomputeSmoothNormals()
    {
        for (render::SceneVertex &v : vertices)
        {
            v.normal = glm::vec3(0.0f);
        }
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const std::uint32_t i0 = indices[i + 0];
            const std::uint32_t i1 = indices[i + 1];
            const std::uint32_t i2 = indices[i + 2];
            const glm::vec3 areaNormal = glm::cross(vertices[i1].position - vertices[i0].position, vertices[i2].position - vertices[i0].position);
            vertices[i0].normal += areaNormal;
            vertices[i1].normal += areaNormal;
            vertices[i2].normal += areaNormal;
        }
        for (render::SceneVertex &v : vertices)
        {
            const float lenSq = glm::dot(v.normal, v.normal);
            v.normal = (lenSq > 1e-10f) ? (v.normal / std::sqrt(lenSq)) : glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }
};

struct PolarContour
{
    float radiusX = 2.15f;
    float radiusZ = 1.60f;
    float phase1 = 0.0f;
    float phase2 = 0.0f;
    float phase3 = 0.0f;

    [[nodiscard]] float modulation(float theta) const noexcept
    {
        return 1.0f + 0.11f * std::sin(2.0f * theta + phase1) + 0.07f * std::cos(3.0f * theta + phase2) + 0.04f * std::sin(5.0f * theta + phase3);
    }

    [[nodiscard]] glm::vec3 pointAt(float theta, float radialScale, float y = 0.0f) const noexcept
    {
        const float r = radialScale * modulation(theta);
        return glm::vec3(radiusX * r * std::cos(theta), y, radiusZ * r * std::sin(theta));
    }
};

[[nodiscard]] glm::vec3 evalCubicBezier(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t) noexcept
{
    const float u = 1.0f - t;
    const float uu = u * u;
    const float tt = t * t;
    return (uu * u) * p0 + (3.0f * uu * t) * p1 + (3.0f * u * tt) * p2 + (tt * t) * p3;
}

[[nodiscard]] glm::vec3 evalCubicBezierTangent(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t) noexcept
{
    const float u = 1.0f - t;
    const glm::vec3 raw = (3.0f * u * u) * (p1 - p0) + (6.0f * u * t) * (p2 - p1) + (3.0f * t * t) * (p3 - p2);
    const float lenSq = glm::dot(raw, raw);
    return (lenSq > 1e-8f) ? (raw / std::sqrt(lenSq)) : glm::vec3(0.0f, 1.0f, 0.0f);
}

[[nodiscard]] glm::mat4 makeLocalTransform(glm::vec3 pos, float yaw, glm::vec3 scale) noexcept
{
    glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
    if (std::abs(yaw) > 1e-5f)
    {
        m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    return glm::scale(m, scale);
}

[[nodiscard]] std::uint32_t buildSandBasinMesh(render::MegaScene &scene, const PolarContour &contour)
{
    constexpr int kAngles = 44;
    constexpr std::array<float, 5> kRingRadii = {0.42f, 0.78f, 0.96f, 1.09f, 1.29f};
    constexpr std::array<float, 5> kRingHeights = {0.012f, 0.050f, 0.138f, 0.165f, 0.004f};
    constexpr std::array<float, 5> kRingU = {0.08f, 0.24f, 0.56f, 0.85f, 0.97f};

    MeshBuilder mb;
    mb.vertices.reserve(1 + kRingRadii.size() * kAngles);
    mb.vertices.push_back({glm::vec3(0.0f, 0.006f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.04f, 0.0f)});

    for (std::size_t r = 0; r < kRingRadii.size(); ++r)
    {
        for (int a = 0; a < kAngles; ++a)
        {
            const float theta = kTwoPi * (static_cast<float>(a) / static_cast<float>(kAngles));
            const glm::vec3 pos = contour.pointAt(theta, kRingRadii[r], kRingHeights[r]);
            const float vCoord = (pos.x + pos.z) * 0.25f;
            mb.vertices.push_back({pos, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(kRingU[r], vCoord)});
        }
    }

    auto emitUpTri = [&](std::uint32_t i0, std::uint32_t i1, std::uint32_t i2)
    {
        const glm::vec3 p0 = mb.vertices[i0].position;
        const glm::vec3 p1 = mb.vertices[i1].position;
        const glm::vec3 p2 = mb.vertices[i2].position;
        if (glm::cross(p1 - p0, p2 - p0).y < 0.0f)
        {
            std::swap(i1, i2);
        }
        mb.indices.push_back(i0);
        mb.indices.push_back(i1);
        mb.indices.push_back(i2);
    };

    for (int a = 0; a < kAngles; ++a)
    {
        const int nextA = (a + 1) % kAngles;
        emitUpTri(0u, static_cast<std::uint32_t>(1 + a), static_cast<std::uint32_t>(1 + nextA));
    }

    for (std::size_t r = 0; r + 1 < kRingRadii.size(); ++r)
    {
        const int ringStart = 1 + static_cast<int>(r) * kAngles;
        const int nextRingStart = ringStart + kAngles;
        for (int a = 0; a < kAngles; ++a)
        {
            const int nextA = (a + 1) % kAngles;
            const auto i00 = static_cast<std::uint32_t>(ringStart + a);
            const auto i01 = static_cast<std::uint32_t>(ringStart + nextA);
            const auto i10 = static_cast<std::uint32_t>(nextRingStart + a);
            const auto i11 = static_cast<std::uint32_t>(nextRingStart + nextA);
            emitUpTri(i00, i10, i11);
            emitUpTri(i00, i11, i01);
        }
    }

    mb.recomputeSmoothNormals();
    return scene.registerMesh(mb.vertices, mb.indices);
}

[[nodiscard]] std::uint32_t buildWaterSurfaceMesh(render::MegaScene &scene, const PolarContour &contour)
{
    constexpr int kAngles = 44;
    constexpr std::array<float, 3> kRings = {0.35f, 0.70f, 0.96f};
    constexpr float kWaterY = 0.120f;

    MeshBuilder mb;
    mb.vertices.reserve(1 + kRings.size() * kAngles);

    auto rippleNormal = [](glm::vec3 pos) -> glm::vec3
    {
        const float nx = 0.032f * std::cos(pos.x * 4.2f + pos.z * 2.5f);
        const float nz = 0.032f * std::sin(pos.z * 4.6f - pos.x * 2.1f);
        return glm::normalize(glm::vec3(nx, 1.0f, nz));
    };

    mb.vertices.push_back({glm::vec3(0.0f, kWaterY, 0.0f), rippleNormal(glm::vec3(0.0f)), glm::vec2(0.0f, 0.0f)});
    for (float rFrac : kRings)
    {
        for (int a = 0; a < kAngles; ++a)
        {
            const float theta = kTwoPi * (static_cast<float>(a) / static_cast<float>(kAngles));
            const glm::vec3 pos = contour.pointAt(theta, rFrac, kWaterY);
            mb.vertices.push_back({pos, rippleNormal(pos), glm::vec2(pos.x * 0.35f, pos.z * 0.35f)});
        }
    }

    auto emitUpTri = [&](std::uint32_t i0, std::uint32_t i1, std::uint32_t i2)
    {
        const glm::vec3 p0 = mb.vertices[i0].position;
        const glm::vec3 p1 = mb.vertices[i1].position;
        const glm::vec3 p2 = mb.vertices[i2].position;
        if (glm::cross(p1 - p0, p2 - p0).y < 0.0f)
        {
            std::swap(i1, i2);
        }
        mb.indices.push_back(i0);
        mb.indices.push_back(i1);
        mb.indices.push_back(i2);
    };

    for (int a = 0; a < kAngles; ++a)
    {
        const int nextA = (a + 1) % kAngles;
        emitUpTri(0u, static_cast<std::uint32_t>(1 + a), static_cast<std::uint32_t>(1 + nextA));
    }
    for (std::size_t r = 0; r + 1 < kRings.size(); ++r)
    {
        const int ringStart = 1 + static_cast<int>(r) * kAngles;
        const int nextRingStart = ringStart + kAngles;
        for (int a = 0; a < kAngles; ++a)
        {
            const int nextA = (a + 1) % kAngles;
            const auto i00 = static_cast<std::uint32_t>(ringStart + a);
            const auto i01 = static_cast<std::uint32_t>(ringStart + nextA);
            const auto i10 = static_cast<std::uint32_t>(nextRingStart + a);
            const auto i11 = static_cast<std::uint32_t>(nextRingStart + nextA);
            emitUpTri(i00, i10, i11);
            emitUpTri(i00, i11, i01);
        }
    }

    return scene.registerMesh(mb.vertices, mb.indices);
}

void appendEllipsoid(MeshBuilder &mb, glm::vec3 center, glm::vec3 radii, float uvU)
{
    constexpr int kStacks = 6;
    constexpr int kSlices = 10;

    std::vector<std::uint32_t> grid(static_cast<std::size_t>((kStacks + 1) * kSlices));
    for (int st = 0; st <= kStacks; ++st)
    {
        const float phi = kPi * (static_cast<float>(st) / static_cast<float>(kStacks));
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);
        for (int sl = 0; sl < kSlices; ++sl)
        {
            const float theta = kTwoPi * (static_cast<float>(sl) / static_cast<float>(kSlices));
            const glm::vec3 dir(sinPhi * std::cos(theta), cosPhi, sinPhi * std::sin(theta));
            const glm::vec3 pos = center + dir * radii;
            const glm::vec3 n = glm::normalize(dir / glm::max(radii, glm::vec3(0.01f)));
            grid[static_cast<std::size_t>(st * kSlices + sl)] = static_cast<std::uint32_t>(mb.vertices.size());
            mb.vertices.push_back({pos, n, glm::vec2(uvU, static_cast<float>(st) / static_cast<float>(kStacks))});
        }
    }

    for (int st = 0; st < kStacks; ++st)
    {
        for (int sl = 0; sl < kSlices; ++sl)
        {
            const int nextSl = (sl + 1) % kSlices;
            const std::uint32_t i00 = grid[static_cast<std::size_t>(st * kSlices + sl)];
            const std::uint32_t i01 = grid[static_cast<std::size_t>(st * kSlices + nextSl)];
            const std::uint32_t i10 = grid[static_cast<std::size_t>((st + 1) * kSlices + sl)];
            const std::uint32_t i11 = grid[static_cast<std::size_t>((st + 1) * kSlices + nextSl)];

            const glm::vec3 triCenter0 = (mb.vertices[i00].position + mb.vertices[i10].position + mb.vertices[i11].position) / 3.0f - center;
            const glm::vec3 triCenter1 = (mb.vertices[i00].position + mb.vertices[i11].position + mb.vertices[i01].position) / 3.0f - center;
            mb.addTriangleOriented(mb.vertices[i00].position, mb.vertices[i10].position, mb.vertices[i11].position, mb.vertices[i00].uv, mb.vertices[i10].uv, mb.vertices[i11].uv, triCenter0);
            mb.addTriangleOriented(mb.vertices[i00].position, mb.vertices[i11].position, mb.vertices[i01].position, mb.vertices[i00].uv, mb.vertices[i11].uv, mb.vertices[i01].uv, triCenter1);
        }
    }
}

struct PalmCrownInfo
{
    std::uint32_t trunkMeshId = 0;
    glm::vec3 rootPos{0.0f};
    glm::vec3 crownPos{0.0f};
};

[[nodiscard]] PalmCrownInfo buildPalmTrunkAndCoconuts(render::MegaScene &scene, const PolarContour &contour, std::mt19937 &rng)
{
    std::uniform_real_distribution<float> angleDist(0.0f, kTwoPi);
    std::uniform_real_distribution<float> heightDist(5.25f, 6.05f);
    std::uniform_real_distribution<float> leanDist(1.35f, 1.80f);

    const float shoreAngle = angleDist(rng);
    const glm::vec3 rootPos = contour.pointAt(shoreAngle, 1.07f, 0.12f);
    const glm::vec3 inward = glm::normalize(glm::vec3(-rootPos.x, 0.0f, -rootPos.z));
    const glm::vec3 side(-inward.z, 0.0f, inward.x);

    const float treeHeight = heightDist(rng);
    const float overhang = leanDist(rng);
    const glm::vec3 p0 = rootPos;
    const glm::vec3 p1 = rootPos + inward * (overhang * 0.22f) + glm::vec3(0.0f, treeHeight * 0.34f, 0.0f);
    const glm::vec3 p2 = rootPos + inward * (overhang * 0.78f) + side * 0.22f + glm::vec3(0.0f, treeHeight * 0.74f, 0.0f);
    const glm::vec3 p3 = rootPos + inward * overhang + side * 0.32f + glm::vec3(0.0f, treeHeight, 0.0f);

    constexpr int kRings = 24;
    constexpr int kSides = 10;

    MeshBuilder mb;
    std::vector<std::uint32_t> ringStarts;
    ringStarts.reserve(kRings + 1);
    std::vector<glm::vec3> ringCenters;
    ringCenters.reserve(kRings + 1);

    glm::vec3 prevTangent = evalCubicBezierTangent(p0, p1, p2, p3, 0.0f);
    glm::vec3 frameRight = glm::normalize(glm::cross(prevTangent, glm::vec3(0.0f, 0.0f, 1.0f)));
    if (glm::dot(frameRight, frameRight) < 0.01f)
    {
        frameRight = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    glm::vec3 frameFwd = glm::normalize(glm::cross(frameRight, prevTangent));

    for (int i = 0; i <= kRings; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kRings);
        const glm::vec3 center = evalCubicBezier(p0, p1, p2, p3, t);
        const glm::vec3 tangent = evalCubicBezierTangent(p0, p1, p2, p3, t);

        frameRight = frameRight - tangent * glm::dot(frameRight, tangent);
        const float rLenSq = glm::dot(frameRight, frameRight);
        frameRight = (rLenSq > 1e-6f) ? (frameRight / std::sqrt(rLenSq)) : glm::vec3(1.0f, 0.0f, 0.0f);
        frameFwd = glm::normalize(glm::cross(tangent, frameRight));

        const float baseRad = glm::mix(0.24f, 0.105f, std::pow(t, 0.72f));
        const float barkStep = 1.0f + 0.095f * std::fmod(t * 16.0f, 1.0f);
        const float rad = baseRad * barkStep;

        ringCenters.push_back(center);
        ringStarts.push_back(static_cast<std::uint32_t>(mb.vertices.size()));
        for (int s = 0; s < kSides; ++s)
        {
            const float phi = kTwoPi * (static_cast<float>(s) / static_cast<float>(kSides));
            const glm::vec3 radial = std::cos(phi) * frameRight + std::sin(phi) * frameFwd;
            const glm::vec3 pos = center + radial * rad;
            const float uCoord = 0.42f + 0.52f * (static_cast<float>(s) / static_cast<float>(kSides));
            mb.vertices.push_back({pos, radial, glm::vec2(uCoord, t * 5.0f)});
        }
    }

    for (int i = 0; i < kRings; ++i)
    {
        const std::uint32_t r0 = ringStarts[static_cast<std::size_t>(i)];
        const std::uint32_t r1 = ringStarts[static_cast<std::size_t>(i + 1)];
        const glm::vec3 segMid = 0.5f * (ringCenters[static_cast<std::size_t>(i)] + ringCenters[static_cast<std::size_t>(i + 1)]);
        for (int s = 0; s < kSides; ++s)
        {
            const int nextS = (s + 1) % kSides;
            const std::uint32_t a = r0 + static_cast<std::uint32_t>(s);
            const std::uint32_t b = r0 + static_cast<std::uint32_t>(nextS);
            const std::uint32_t c = r1 + static_cast<std::uint32_t>(s);
            const std::uint32_t d = r1 + static_cast<std::uint32_t>(nextS);
            const glm::vec3 hint0 = ((mb.vertices[a].position + mb.vertices[b].position + mb.vertices[d].position) / 3.0f) - segMid;
            const glm::vec3 hint1 = ((mb.vertices[a].position + mb.vertices[d].position + mb.vertices[c].position) / 3.0f) - segMid;
            mb.addTriangleOriented(mb.vertices[a].position, mb.vertices[b].position, mb.vertices[d].position, mb.vertices[a].uv, mb.vertices[b].uv, mb.vertices[d].uv, hint0);
            mb.addTriangleOriented(mb.vertices[a].position, mb.vertices[d].position, mb.vertices[c].position, mb.vertices[a].uv, mb.vertices[d].uv, mb.vertices[c].uv, hint1);
        }
    }

    // Coconut cluster tucked cleanly under the crown
    for (int c = 0; c < 3; ++c)
    {
        const float cAngle = kTwoPi * (static_cast<float>(c) / 3.0f) + 0.4f;
        const glm::vec3 offset(std::cos(cAngle) * 0.16f, -0.17f - 0.03f * static_cast<float>(c % 2), std::sin(cAngle) * 0.16f);
        appendEllipsoid(mb, p3 + offset, glm::vec3(0.115f, 0.145f, 0.115f), 0.14f);
    }

    const std::uint32_t trunkMeshId = scene.registerMesh(mb.vertices, mb.indices);
    return PalmCrownInfo{trunkMeshId, rootPos, p3};
}

void appendPalmFrond(MeshBuilder &mb, glm::vec3 origin, float yaw, float liftPitch, float length, float maxHalfWidth, float droop)
{
    const glm::vec3 dir(std::cos(yaw), 0.0f, std::sin(yaw));
    const glm::vec3 right(-dir.z, 0.0f, dir.x);

    const glm::vec3 p0 = origin;
    const glm::vec3 p1 = origin + dir * (length * 0.30f) + glm::vec3(0.0f, liftPitch, 0.0f);
    const glm::vec3 p2 = origin + dir * (length * 0.70f) + glm::vec3(0.0f, liftPitch * 0.45f - droop * 0.25f, 0.0f);
    const glm::vec3 p3 = origin + dir * length + glm::vec3(0.0f, -droop, 0.0f);

    constexpr int kSteps = 12;
    for (int i = 0; i < kSteps; ++i)
    {
        const float s0 = static_cast<float>(i) / static_cast<float>(kSteps);
        const float s1 = static_cast<float>(i + 1) / static_cast<float>(kSteps);
        const glm::vec3 c0 = evalCubicBezier(p0, p1, p2, p3, s0);
        const glm::vec3 c1 = evalCubicBezier(p0, p1, p2, p3, s1);

        const float wMid0 = 0.035f;
        const float wMid1 = 0.035f * (1.0f - 0.65f * s1);
        const float sway0 = std::clamp(0.12f + 0.85f * s0, 0.0f, 1.0f);
        const float sway1 = std::clamp(0.12f + 0.85f * s1, 0.0f, 1.0f);

        // Central folded V-rib strip
        mb.addDoubleSidedQuad(c0 - right * wMid0 + glm::vec3(0.0f, 0.02f, 0.0f), c0, c1, c1 - right * wMid1 + glm::vec3(0.0f, 0.02f, 0.0f), {0.10f, sway0}, {0.02f, sway0}, {0.02f, sway1}, {0.10f, sway1});
        mb.addDoubleSidedQuad(c0, c0 + right * wMid0 + glm::vec3(0.0f, 0.02f, 0.0f), c1 + right * wMid1 + glm::vec3(0.0f, 0.02f, 0.0f), c1, {0.02f, sway0}, {0.10f, sway0}, {0.10f, sway1}, {0.02f, sway1});

        // Serrated feathered leaflets fanning outward & drooping slightly
        const float env = std::sin(kPi * std::clamp(s0 * 0.90f + 0.08f, 0.0f, 1.0f));
        const float leafletLen = maxHalfWidth * env;
        if (leafletLen > 0.05f)
        {
            const glm::vec3 forwardReach = (c1 - c0) * 1.15f;
            const glm::vec3 bladeDrop(0.0f, -0.15f * env, 0.0f);
            const glm::vec3 tipLeft = c0 - right * leafletLen + forwardReach + bladeDrop;
            const glm::vec3 tipRight = c0 + right * leafletLen + forwardReach + bladeDrop;
            const glm::vec3 baseB = glm::mix(c0, c1, 0.72f);

            mb.addDoubleSidedTri(c0, tipLeft, baseB, {0.15f, sway0}, {0.95f, sway1}, {0.22f, sway1});
            mb.addDoubleSidedTri(c0, baseB, tipRight, {0.15f, sway0}, {0.22f, sway1}, {0.95f, sway1});
        }
    }
}

void appendFernRosette(MeshBuilder &mb, glm::vec3 center, int bladeCount, float maxLength, std::mt19937 &rng)
{
    std::uniform_real_distribution<float> jitter(-0.18f, 0.18f);
    std::uniform_real_distribution<float> scaleDist(0.75f, 1.15f);

    for (int b = 0; b < bladeCount; ++b)
    {
        const float yaw = kTwoPi * (static_cast<float>(b) / static_cast<float>(bladeCount)) + jitter(rng);
        const float len = maxLength * scaleDist(rng);
        const glm::vec3 dir(std::cos(yaw), 0.0f, std::sin(yaw));
        const glm::vec3 right(-dir.z, 0.0f, dir.x);

        const glm::vec3 p0 = center + dir * 0.05f;
        const glm::vec3 p1 = center + dir * (len * 0.35f) + glm::vec3(0.0f, len * 0.58f, 0.0f);
        const glm::vec3 p2 = center + dir * (len * 0.78f) + glm::vec3(0.0f, len * 0.45f, 0.0f);
        const glm::vec3 p3 = center + dir * len + glm::vec3(0.0f, len * 0.12f, 0.0f);

        constexpr int kSegs = 4;
        for (int s = 0; s < kSegs; ++s)
        {
            const float t0 = static_cast<float>(s) / static_cast<float>(kSegs);
            const float t1 = static_cast<float>(s + 1) / static_cast<float>(kSegs);
            const glm::vec3 a = evalCubicBezier(p0, p1, p2, p3, t0);
            const glm::vec3 c = evalCubicBezier(p0, p1, p2, p3, t1);
            const float w0 = 0.09f * std::sin(kPi * std::clamp(0.15f + 0.85f * t0, 0.0f, 1.0f));
            const float w1 = (s + 1 == kSegs) ? 0.008f : 0.09f * std::sin(kPi * std::clamp(0.15f + 0.85f * t1, 0.0f, 1.0f));
            const float sway0 = 0.45f * t0;
            const float sway1 = 0.45f * t1;
            mb.addDoubleSidedQuad(a - right * w0, a + right * w0, c + right * w1, c - right * w1, {0.25f, sway0}, {0.85f, sway0}, {0.85f, sway1}, {0.25f, sway1});
        }
    }
}

void appendReedClump(MeshBuilder &mb, glm::vec3 center, int bladeCount, std::mt19937 &rng)
{
    std::uniform_real_distribution<float> offDist(-0.18f, 0.18f);
    std::uniform_real_distribution<float> heightDist(0.62f, 1.05f);
    std::uniform_real_distribution<float> angleDist(0.0f, kTwoPi);

    for (int i = 0; i < bladeCount; ++i)
    {
        const float yaw = angleDist(rng);
        const float h = heightDist(rng);
        const glm::vec3 base = center + glm::vec3(offDist(rng), 0.0f, offDist(rng));
        const glm::vec3 lean(std::cos(yaw) * 0.22f, h, std::sin(yaw) * 0.22f);
        const glm::vec3 side(-std::sin(yaw) * 0.025f, 0.0f, std::cos(yaw) * 0.025f);

        const glm::vec3 mid = base + glm::vec3(lean.x * 0.35f, h * 0.55f, lean.z * 0.35f);
        const glm::vec3 tip = base + lean;
        mb.addDoubleSidedQuad(base - side, base + side, mid + side * 0.7f, mid - side * 0.7f, {0.20f, 0.0f}, {0.75f, 0.0f}, {0.75f, 0.28f}, {0.20f, 0.28f});
        mb.addDoubleSidedTri(mid - side * 0.7f, mid + side * 0.7f, tip, {0.20f, 0.28f}, {0.75f, 0.28f}, {0.50f, 0.65f});
    }
}

[[nodiscard]] std::uint32_t buildFoliageMesh(render::MegaScene &scene, const PolarContour &contour, const PalmCrownInfo &palm, std::mt19937 &rng)
{
    MeshBuilder mb;

    // 1. Main arched outer palm fronds + 3 steeper inner crown fronds
    constexpr int kOuterFronds = 7;
    for (int f = 0; f < kOuterFronds; ++f)
    {
        const float yaw = kTwoPi * (static_cast<float>(f) / static_cast<float>(kOuterFronds)) + 0.18f * std::sin(static_cast<float>(f) * 2.1f);
        const float len = 2.25f + 0.30f * static_cast<float>(f % 2);
        appendPalmFrond(mb, palm.crownPos, yaw, 0.62f, len, 0.48f, 1.15f);
    }
    constexpr int kInnerFronds = 3;
    for (int f = 0; f < kInnerFronds; ++f)
    {
        const float yaw = kTwoPi * (static_cast<float>(f) / static_cast<float>(kInnerFronds)) + 0.55f;
        appendPalmFrond(mb, palm.crownPos + glm::vec3(0.0f, 0.06f, 0.0f), yaw, 0.92f, 1.65f, 0.36f, 0.45f);
    }

    // 2. Tropical broad-leaf fern rosettes near palm base and shore rocks
    appendFernRosette(mb, palm.rootPos + glm::vec3(0.28f, 0.0f, 0.16f), 8, 0.95f, rng);
    appendFernRosette(mb, palm.rootPos + glm::vec3(-0.32f, 0.0f, -0.22f), 7, 0.82f, rng);
    for (int k = 0; k < 3; ++k)
    {
        const float angle = kTwoPi * (static_cast<float>(k) / 3.0f) + 1.1f;
        const glm::vec3 pos = contour.pointAt(angle, 1.07f, 0.13f);
        appendFernRosette(mb, pos, 6, 0.72f, rng);
    }

    // 3. Water-edge reeds right along the damp inner margin
    for (int r = 0; r < 3; ++r)
    {
        const float angle = kTwoPi * (static_cast<float>(r) / 3.0f) + 0.35f;
        const glm::vec3 pos = contour.pointAt(angle, 0.88f, 0.08f);
        appendReedClump(mb, pos, 8, rng);
    }

    return scene.registerMesh(mb.vertices, mb.indices);
}

} // namespace

void buildMazeOases(const MazeMapData &map, render::MegaScene &scene, std::span<const std::uint32_t> rockMeshes, std::uint32_t emberMeshId, const OasisMaterialIds &materials, std::vector<render::GpuPointLight> &pointLights)
{
    for (const OasisSpawn &oasis : map.oases)
    {
        std::mt19937 rng(oasis.seed);
        std::uniform_real_distribution<float> phaseDist(0.0f, kTwoPi);
        std::uniform_real_distribution<float> rockScaleDist(0.22f, 0.52f);

        const PolarContour contour{
            .radiusX = oasis.radiusX,
            .radiusZ = oasis.radiusZ,
            .phase1 = phaseDist(rng),
            .phase2 = phaseDist(rng),
            .phase3 = phaseDist(rng),
        };

        const glm::mat4 oasisModel = makeLocalTransform(oasis.center, oasis.yaw, glm::vec3(1.0f));

        // 1. Terraced sand bank + transparent depth-aware desert spring water
        const std::uint32_t basinMeshId = buildSandBasinMesh(scene, contour);
        const std::uint32_t waterMeshId = buildWaterSurfaceMesh(scene, contour);
        scene.addObject(basinMeshId, materials.sandBank, oasisModel);
        scene.addObject(waterMeshId, materials.waterSurface, oasisModel, render::kObjectVisibleFlag | render::kObjectWaterFlag);

        // 2. Curved leaning palm tree + coconuts + wind-swaying serrated fronds & ferns
        const PalmCrownInfo palm = buildPalmTrunkAndCoconuts(scene, contour, rng);
        const std::uint32_t foliageMeshId = buildFoliageMesh(scene, contour, palm, rng);
        scene.addObject(palm.trunkMeshId, materials.palmBark, oasisModel);
        scene.addObject(foliageMeshId, materials.tropicalFoliage, oasisModel, render::kObjectVisibleFlag | render::kObjectWindSwayFlag);

        // 3. Shoreline rock ring (clustered heavier near palm base)
        if (!rockMeshes.empty())
        {
            const std::uint32_t rockFlags = render::kObjectVisibleFlag | render::kObjectWorldUVFlag;
            constexpr int kShoreRocks = 14;
            for (int i = 0; i < kShoreRocks; ++i)
            {
                const float angle = kTwoPi * (static_cast<float>(i) / static_cast<float>(kShoreRocks)) + phaseDist(rng) * 0.08f;
                const glm::vec3 localPos = contour.pointAt(angle, 0.99f + 0.12f * static_cast<float>(i % 2), 0.14f);
                const float distToPalm = glm::length(glm::vec2(localPos.x - palm.rootPos.x, localPos.z - palm.rootPos.z));
                const float scaleBoost = (distToPalm < 1.25f) ? 1.35f : 1.0f;
                const float s = rockScaleDist(rng) * scaleBoost;
                const glm::mat4 rockLocal = makeLocalTransform(localPos + glm::vec3(0.0f, s * 0.22f, 0.0f), phaseDist(rng), glm::vec3(s * 1.15f, s * 0.80f, s));
                scene.addObject(rockMeshes[static_cast<std::size_t>(i) % rockMeshes.size()], materials.stoneWall, oasisModel * rockLocal, rockFlags);
            }
        }

        // 4. Warm camp ember crystal tucked by the palm trunk so the oasis catches dramatic warm highlights
        const glm::vec3 worldPalmRoot = glm::vec3(oasisModel * glm::vec4(palm.rootPos, 1.0f));
        const glm::vec3 emberWorld = worldPalmRoot + glm::normalize(oasis.center - worldPalmRoot) * 0.45f + glm::vec3(0.0f, 0.22f, 0.0f);
        scene.addObject(emberMeshId, materials.warmEmber, makeLocalTransform(emberWorld, 0.5f, glm::vec3(0.18f, 0.24f, 0.18f)));
        pointLights.push_back(render::GpuPointLight{
            .positionRadius = glm::vec4(emberWorld + glm::vec3(0.0f, 0.35f, 0.0f), 7.2f),
            .colorIntensity = glm::vec4(1.0f, 0.64f, 0.28f, 2.5f),
        });
    }
}

} // namespace game
