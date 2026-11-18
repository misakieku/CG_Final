#include "game/FoliageGenerator.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

namespace game
{

namespace
{

constexpr float kTwoPi = glm::two_pi<float>();
constexpr float kPi = glm::pi<float>();

struct MeshBuilder
{
    std::vector<render::SceneVertex> vertices;
    std::vector<std::uint32_t> indices;

    void addDoubleSidedTri(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec2 uva, glm::vec2 uvb, glm::vec2 uvc)
    {
        const glm::vec3 cr = glm::cross(b - a, c - a);
        const float len = glm::length(cr);
        const glm::vec3 rawN = (len > 1e-6f) ? (cr / len) : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 upSoftN = glm::normalize(glm::mix(rawN, glm::vec3(0.0f, 1.0f, 0.0f), 0.25f));
        const glm::vec3 backSoftN = glm::normalize(glm::mix(-rawN, glm::vec3(0.0f, 1.0f, 0.0f), 0.25f));

        const std::uint32_t f0 = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back({a, upSoftN, uva});
        vertices.push_back({b, upSoftN, uvb});
        vertices.push_back({c, upSoftN, uvc});
        indices.insert(indices.end(), {f0, f0 + 1u, f0 + 2u});

        const std::uint32_t b0 = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back({a, backSoftN, uva});
        vertices.push_back({c, backSoftN, uvc});
        vertices.push_back({b, backSoftN, uvb});
        indices.insert(indices.end(), {b0, b0 + 1u, b0 + 2u});
    }
};

[[nodiscard]] glm::vec3 evalCubicBezier(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t) noexcept
{
    const float u = 1.0f - t;
    return (u * u * u) * p0 + (3.0f * u * u * t) * p1 + (3.0f * u * t * t) * p2 + (t * t * t) * p3;
}

[[nodiscard]] glm::vec3 evalCubicBezierTangent(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t) noexcept
{
    const float u = 1.0f - t;
    const glm::vec3 d = (3.0f * u * u) * (p1 - p0) + (6.0f * u * t) * (p2 - p1) + (3.0f * t * t) * (p3 - p2);
    const float lenSq = glm::dot(d, d);
    return (lenSq > 1e-8f) ? (d / std::sqrt(lenSq)) : glm::vec3(0.0f, 1.0f, 0.0f);
}

// V-folded lanceolate/ovate leaf along an arbitrary cubic Bezier midrib with crisp vein UVs (u=0.56 midrib)
void appendFoldedBlade(MeshBuilder &mb, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float maxHalfWidth, float vStart, float vEnd, int steps, bool arrowheadProfile = false)
{
    const glm::vec3 chord = p3 - p0;
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

    auto bladeWidth = [&](float t) -> float
    {
        if (t >= 0.999f)
        {
            return 0.0f;
        }
        if (arrowheadProfile)
        {
            // Broad heart/arrowhead base peaking around t = 0.24 then tapering gracefully to the tip
            const float swell = std::sin(kPi * std::pow(std::clamp(t, 0.0f, 1.0f), 0.45f));
            const float taper = 1.0f - 0.35f * t;
            return maxHalfWidth * swell * taper;
        }
        return maxHalfWidth * std::sin(kPi * std::pow(std::clamp(0.08f + 0.92f * t, 0.0f, 1.0f), 0.72f));
    };

    for (int s = 0; s < steps; ++s)
    {
        const float t0 = static_cast<float>(s) / static_cast<float>(steps);
        const float t1 = static_cast<float>(s + 1) / static_cast<float>(steps);
        const glm::vec3 c0 = evalCubicBezier(p0, p1, p2, p3, t0);
        const glm::vec3 c1 = evalCubicBezier(p0, p1, p2, p3, t1);
        const glm::vec3 tan0 = evalCubicBezierTangent(p0, p1, p2, p3, t0);
        const glm::vec3 tan1 = evalCubicBezierTangent(p0, p1, p2, p3, t1);

        auto sideFrame = [&](glm::vec3 tan, glm::vec3 &outRight, glm::vec3 &outUp)
        {
            glm::vec3 r = glm::cross(tan, worldUp);
            if (glm::dot(r, r) < 1e-5f)
            {
                r = glm::cross(glm::vec3(chord.x, 0.0f, chord.z) + glm::vec3(1e-3f, 0.0f, 0.0f), worldUp);
            }
            outRight = glm::normalize(r);
            outUp = glm::normalize(glm::cross(outRight, tan));
        };

        glm::vec3 right0{}, up0{}, right1{}, up1{};
        sideFrame(tan0, right0, up0);
        sideFrame(tan1, right1, up1);

        const float w0 = bladeWidth(t0);
        const float w1 = (s + 1 == steps) ? 0.0f : bladeWidth(t1);

        const float crease0 = w0 * 0.22f;
        const float crease1 = w1 * 0.22f;
        const float v0 = glm::mix(vStart, vEnd, t0);
        const float v1 = glm::mix(vStart, vEnd, t1);

        const glm::vec3 spine0 = c0 + up0 * (crease0 * 0.5f);
        const glm::vec3 spine1 = c1 + up1 * (crease1 * 0.5f);
        const glm::vec3 l0 = c0 - right0 * w0 - up0 * crease0;
        const glm::vec3 r0 = c0 + right0 * w0 - up0 * crease0;

        if (s + 1 < steps)
        {
            const glm::vec3 l1 = c1 - right1 * w1 - up1 * crease1;
            const glm::vec3 r1 = c1 + right1 * w1 - up1 * crease1;
            mb.addDoubleSidedTri(spine0, l0, l1, {0.56f, v0}, {0.16f, v0}, {0.16f, v1});
            mb.addDoubleSidedTri(spine0, l1, spine1, {0.56f, v0}, {0.16f, v1}, {0.56f, v1});
            mb.addDoubleSidedTri(spine0, r1, r0, {0.56f, v0}, {0.96f, v1}, {0.96f, v0});
            mb.addDoubleSidedTri(spine0, spine1, r1, {0.56f, v0}, {0.56f, v1}, {0.96f, v1});
        }
        else
        {
            mb.addDoubleSidedTri(spine0, l0, spine1, {0.56f, v0}, {0.16f, v0}, {0.56f, v1});
            mb.addDoubleSidedTri(spine0, spine1, r0, {0.56f, v0}, {0.56f, v1}, {0.96f, v0});
        }
    }
}

void appendCurvedStemAndPoints(MeshBuilder &mb, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float baseRadius, int segs, std::vector<glm::vec3> &outPoints, std::vector<glm::vec3> &outTangents)
{
    outPoints.clear();
    outTangents.clear();
    std::uint32_t prevStart = 0;

    for (int i = 0; i <= segs; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(segs);
        const glm::vec3 c = evalCubicBezier(p0, p1, p2, p3, t);
        const glm::vec3 tan = evalCubicBezierTangent(p0, p1, p2, p3, t);
        outPoints.push_back(c);
        outTangents.push_back(tan);

        glm::vec3 r = glm::cross(tan, glm::vec3(0.0f, 1.0f, 0.0f));
        if (glm::dot(r, r) < 1e-5f)
        {
            r = glm::vec3(1.0f, 0.0f, 0.0f);
        }
        r = glm::normalize(r);
        const glm::vec3 u = glm::normalize(glm::cross(r, tan));
        const float rad = glm::mix(baseRadius, baseRadius * 0.30f, t);
        const float swayV = glm::clamp(c.y * 1.15f, 0.0f, 0.85f);

        const std::uint32_t curStart = static_cast<std::uint32_t>(mb.vertices.size());
        constexpr std::array<glm::vec2, 3> kTriRing = {{{1.0f, 0.0f}, {-0.5f, 0.866025f}, {-0.5f, -0.866025f}}};
        for (int k = 0; k < 3; ++k)
        {
            const glm::vec3 n = r * kTriRing[static_cast<std::size_t>(k)].x + u * kTriRing[static_cast<std::size_t>(k)].y;
            mb.vertices.push_back({c + n * rad, n, glm::vec2(0.05f, swayV)});
        }
        if (i > 0)
        {
            for (int k = 0; k < 3; ++k)
            {
                const int kn = (k + 1) % 3;
                const auto a0 = prevStart + static_cast<std::uint32_t>(k);
                const auto a1 = prevStart + static_cast<std::uint32_t>(kn);
                const auto b0 = curStart + static_cast<std::uint32_t>(k);
                const auto b1 = curStart + static_cast<std::uint32_t>(kn);
                mb.indices.insert(mb.indices.end(), {a0, a1, b1, a0, b1, b0});
            }
        }
        prevStart = curStart;
    }
}

void appendRosetteTier(MeshBuilder &mb, glm::vec3 origin, int leafCount, float yawOffset, float lenMin, float lenMax, float halfW, float pitchRad, float swayBase, std::mt19937 &rng)
{
    std::uniform_real_distribution<float> jitter(-0.16f, 0.16f);
    std::uniform_real_distribution<float> lenRand(lenMin, lenMax);

    for (int i = 0; i < leafCount; ++i)
    {
        const float yaw = yawOffset + kTwoPi * (static_cast<float>(i) / static_cast<float>(leafCount)) + jitter(rng);
        const float len = lenRand(rng);
        const glm::vec3 dir(std::cos(yaw), 0.0f, std::sin(yaw));

        const float cosP = std::cos(pitchRad);
        const float sinP = std::sin(pitchRad);
        const glm::vec3 p0 = origin + dir * 0.02f;
        const glm::vec3 p1 = p0 + dir * (len * 0.34f * cosP) + glm::vec3(0.0f, len * 0.40f * sinP + 0.05f, 0.0f);
        const glm::vec3 p2 = p0 + dir * (len * 0.72f * cosP) + glm::vec3(0.0f, len * 0.56f * sinP + 0.03f, 0.0f);
        const glm::vec3 p3 = p0 + dir * (len * 0.96f * cosP) + glm::vec3(0.0f, len * 0.38f * sinP - 0.03f, 0.0f);
        appendFoldedBlade(mb, p0, p1, p2, p3, halfW * (0.9f + 0.2f * static_cast<float>(i % 2)), swayBase, std::min(0.92f, swayBase + 0.42f), 3);
    }
}

// Variant 0: Low multi-tier spiral rosette of V-folded pointed broad leaves
[[nodiscard]] std::uint32_t buildRosetteBushMesh(render::MegaScene &scene, std::uint32_t seed)
{
    std::mt19937 rng(seed);
    MeshBuilder mb;
    appendRosetteTier(mb, glm::vec3(0.0f, 0.01f, 0.0f), 8, 0.10f, 0.38f, 0.48f, 0.105f, 0.26f, 0.10f, rng);
    appendRosetteTier(mb, glm::vec3(0.0f, 0.03f, 0.0f), 7, 0.52f, 0.30f, 0.38f, 0.092f, 0.62f, 0.28f, rng);
    appendRosetteTier(mb, glm::vec3(0.0f, 0.05f, 0.0f), 5, 0.24f, 0.19f, 0.25f, 0.074f, 1.05f, 0.46f, rng);
    return scene.registerMesh(mb.vertices, mb.indices);
}

// Variant 1: Airy multi-stem arching fern frond bush with paired pointed leaflets
[[nodiscard]] std::uint32_t buildPinnateFernMesh(render::MegaScene &scene, std::uint32_t seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> jitter(-0.22f, 0.22f);
    std::uniform_real_distribution<float> reachRand(0.40f, 0.62f);
    std::uniform_real_distribution<float> heightRand(0.40f, 0.66f);
    MeshBuilder mb;

    std::vector<glm::vec3> pts, tans;
    constexpr int kFronds = 6;
    constexpr int kStemSegs = 6;

    for (int f = 0; f < kFronds; ++f)
    {
        const float yaw = kTwoPi * (static_cast<float>(f) / static_cast<float>(kFronds)) + jitter(rng);
        const float reach = reachRand(rng);
        const float height = heightRand(rng);
        const glm::vec3 dir(std::cos(yaw), 0.0f, std::sin(yaw));

        const glm::vec3 root = dir * 0.03f + glm::vec3(0.0f, -0.01f, 0.0f);
        const glm::vec3 p1 = root + dir * (reach * 0.18f) + glm::vec3(0.0f, height * 0.58f, 0.0f);
        const glm::vec3 p2 = root + dir * (reach * 0.62f) + glm::vec3(0.0f, height * 1.02f, 0.0f);
        const glm::vec3 tip = root + dir * reach + glm::vec3(0.0f, height * 0.55f, 0.0f);

        appendCurvedStemAndPoints(mb, root, p1, p2, tip, 0.012f, kStemSegs, pts, tans);

        // Paired lanceolate leaflets fanning forward along upper 70% of each frond stem
        for (int s = 2; s < kStemSegs; ++s)
        {
            const float st = static_cast<float>(s) / static_cast<float>(kStemSegs);
            const glm::vec3 anchor = pts[static_cast<std::size_t>(s)];
            const glm::vec3 fwd = tans[static_cast<std::size_t>(s)];
            const glm::vec3 side = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
            const float leafLen = glm::mix(0.14f, 0.24f, std::sin(kPi * (st - 0.15f)));
            const float leafW = 0.038f * (1.0f - 0.22f * st);
            const float vBase = glm::clamp(st * 0.60f, 0.12f, 0.65f);

            for (float sign : {-1.0f, 1.0f})
            {
                const glm::vec3 leafDir = glm::normalize(fwd * 0.65f + side * (sign * 0.85f) + glm::vec3(0.0f, 0.15f, 0.0f));
                const glm::vec3 lp1 = anchor + leafDir * (leafLen * 0.35f) + glm::vec3(0.0f, 0.025f, 0.0f);
                const glm::vec3 lp2 = anchor + leafDir * (leafLen * 0.72f) + glm::vec3(0.0f, 0.010f, 0.0f);
                const glm::vec3 lp3 = anchor + leafDir * leafLen + glm::vec3(0.0f, -0.025f, 0.0f);
                appendFoldedBlade(mb, anchor, lp1, lp2, lp3, leafW, vBase, std::min(0.95f, vBase + 0.30f), 2);
            }
        }

        // Terminal tip leaflet extending the stem smoothly
        const glm::vec3 tipFwd = tans.back();
        const glm::vec3 tp1 = tip + tipFwd * 0.05f;
        const glm::vec3 tp2 = tip + tipFwd * 0.11f + glm::vec3(0.0f, -0.01f, 0.0f);
        const glm::vec3 tp3 = tip + tipFwd * 0.16f + glm::vec3(0.0f, -0.03f, 0.0f);
        appendFoldedBlade(mb, tip, tp1, tp2, tp3, 0.030f, 0.65f, 0.95f, 2);
    }

    return scene.registerMesh(mb.vertices, mb.indices);
}

// Variant 2: Slender-stemmed arching arrowhead/taro broad-leaf cluster at staggered heights
[[nodiscard]] std::uint32_t buildStemmedBroadleafMesh(render::MegaScene &scene, std::uint32_t seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> jitter(-0.26f, 0.26f);
    std::uniform_real_distribution<float> heightRand(0.24f, 0.64f);
    std::uniform_real_distribution<float> reachRand(0.14f, 0.36f);
    MeshBuilder mb;

    appendRosetteTier(mb, glm::vec3(0.0f, 0.01f, 0.0f), 5, 0.35f, 0.22f, 0.30f, 0.075f, 0.44f, 0.10f, rng);

    std::vector<glm::vec3> pts, tans;
    constexpr int kStems = 8;
    for (int i = 0; i < kStems; ++i)
    {
        const float yaw = kTwoPi * (static_cast<float>(i) / static_cast<float>(kStems)) + jitter(rng);
        const float tierMul = (i % 2 == 0) ? 1.0f : 0.65f;
        const float height = heightRand(rng) * tierMul;
        const float reach = reachRand(rng);
        const glm::vec3 dir(std::cos(yaw), 0.0f, std::sin(yaw));

        const glm::vec3 root = dir * 0.025f + glm::vec3(0.0f, -0.01f, 0.0f);
        const glm::vec3 p1 = root + dir * (reach * 0.25f) + glm::vec3(0.0f, height * 0.45f, 0.0f);
        const glm::vec3 p2 = root + dir * (reach * 0.70f) + glm::vec3(0.0f, height * 0.85f, 0.0f);
        const glm::vec3 top = root + dir * reach + glm::vec3(0.0f, height, 0.0f);

        appendCurvedStemAndPoints(mb, root, p1, p2, top, 0.010f, 3, pts, tans);

        const float bladeLen = 0.22f + 0.07f * static_cast<float>(i % 2);
        const float bladeW = bladeLen * 0.46f;
        const glm::vec3 lp1 = top + dir * (bladeLen * 0.30f) + glm::vec3(0.0f, 0.045f, 0.0f);
        const glm::vec3 lp2 = top + dir * (bladeLen * 0.68f) - glm::vec3(0.0f, 0.015f, 0.0f);
        const glm::vec3 lp3 = top + dir * (bladeLen * 0.96f) - glm::vec3(0.0f, 0.095f, 0.0f);
        appendFoldedBlade(mb, top - dir * 0.03f, lp1, lp2, lp3, bladeW, 0.40f, 0.94f, 4, true);
    }

    return scene.registerMesh(mb.vertices, mb.indices);
}

[[nodiscard]] float signedDistanceToObb(glm::vec2 p, const FloorObstacleObb &obb) noexcept
{
    const glm::vec2 d = p - obb.center;
    const float lx = std::abs(glm::dot(d, obb.axisX)) - obb.halfExtents.x;
    const float lz = std::abs(glm::dot(d, obb.axisZ)) - obb.halfExtents.y;
    const glm::vec2 outside(std::max(lx, 0.0f), std::max(lz, 0.0f));
    return glm::length(outside) + std::min(std::max(lx, lz), 0.0f);
}

[[nodiscard]] float minSignedDistanceToObstacles(glm::vec2 p, std::span<const std::vector<std::uint32_t>> cellBuckets, const std::vector<FloorObstacleObb> &obstacles, const MazeMapData &map) noexcept
{
    const glm::ivec2 cell = map.worldToCell(glm::vec3(p.x, 0.0f, p.y));
    float minDist = 100.0f;
    for (int dz = -1; dz <= 1; ++dz)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            const glm::ivec2 nb(cell.x + dx, cell.y + dz);
            if (!map.inBounds(nb))
            {
                continue;
            }
            const auto &bucket = cellBuckets[static_cast<std::size_t>(map.getCellIndex(nb.x, nb.y))];
            for (std::uint32_t idx : bucket)
            {
                minDist = std::min(minDist, signedDistanceToObb(p, obstacles[idx]));
            }
        }
    }
    return minDist;
}

[[nodiscard]] std::vector<std::vector<std::uint32_t>> buildObstacleBuckets(const MazeMapData &map, const std::vector<FloorObstacleObb> &obstacles)
{
    std::vector<std::vector<std::uint32_t>> buckets(map.cells.size());
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(obstacles.size()); ++i)
    {
        const glm::ivec2 cell = map.worldToCell(glm::vec3(obstacles[i].center.x, 0.0f, obstacles[i].center.y));
        if (map.inBounds(cell))
        {
            buckets[static_cast<std::size_t>(map.getCellIndex(cell.x, cell.y))].push_back(i);
        }
    }
    return buckets;
}

[[nodiscard]] bool isGameplayClear(const MazeMapData &map, glm::vec2 posXZ, float pad) noexcept
{
    auto tooCloseSq = [&](glm::vec3 anchor, float radius) -> bool
    {
        const glm::vec2 d = posXZ - glm::vec2(anchor.x, anchor.z);
        return glm::dot(d, d) < (radius + pad) * (radius + pad);
    };

    if (tooCloseSq(map.playerStart, 1.15f) || tooCloseSq(map.exitPos, 1.15f))
    {
        return false;
    }
    for (const glm::vec3 &tp : map.treasurePositions)
    {
        if (tooCloseSq(tp, 1.00f))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] float fractGlsl(float x) noexcept
{
    return x - std::floor(x);
}

[[nodiscard]] float hash21Puddle(glm::vec2 p) noexcept
{
    p = glm::vec2(fractGlsl(p.x * 123.34f), fractGlsl(p.y * 456.21f));
    const float d = glm::dot(p, p + glm::vec2(45.32f));
    p.x += d;
    p.y += d;
    return fractGlsl(p.x * p.y);
}

[[nodiscard]] float valueNoisePuddle(glm::vec2 p) noexcept
{
    const glm::vec2 i(std::floor(p.x), std::floor(p.y));
    const glm::vec2 f = p - i;
    const glm::vec2 u = f * f * (glm::vec2(3.0f) - 2.0f * f);
    const float a = hash21Puddle(i);
    const float b = hash21Puddle(i + glm::vec2(1.0f, 0.0f));
    const float c = hash21Puddle(i + glm::vec2(0.0f, 1.0f));
    const float d = hash21Puddle(i + glm::vec2(1.0f, 1.0f));
    return glm::mix(glm::mix(a, b, u.x), glm::mix(c, d, u.x), u.y);
}

[[nodiscard]] float evalPuddleBasin(glm::vec2 worldXZ) noexcept
{
    auto fbm = [](glm::vec2 p) -> float
    {
        float v = 0.0f;
        float a = 0.5f;
        for (int i = 0; i < 4; ++i)
        {
            v += a * valueNoisePuddle(p);
            p = p * 2.03f + glm::vec2(17.1f, 31.7f);
            a *= 0.5f;
        }
        return v;
    };

    const glm::vec2 warp(fbm(worldXZ * 0.28f), fbm(worldXZ * 0.28f + glm::vec2(9.2f, 4.7f)));
    return fbm(worldXZ * 0.38f + (warp - glm::vec2(0.5f)) * 0.75f);
}

} // namespace

FloorObstacleObb FloorObstacleObb::fromYaw(glm::vec2 worldXZ, float yawRad, glm::vec2 halfExt, bool surfaceProp) noexcept
{
    // Matches glm::rotate(yawRad, +Y): local +X maps to (cos, -sin), local +Z maps to (sin, cos)
    const float c = std::cos(yawRad);
    const float s = std::sin(yawRad);
    return FloorObstacleObb{
        .center = worldXZ,
        .axisX = glm::vec2(c, -s),
        .axisZ = glm::vec2(s, c),
        .halfExtents = halfExt,
        .isSurfaceProp = surfaceProp,
    };
}

std::vector<glm::vec3> populateCourtyardBushes(const MazeMapData &map, render::MegaScene &scene, std::uint32_t bushMaterialId, std::vector<FloorObstacleObb> &obstacles, const FoliageGenDesc &desc)
{
    std::vector<glm::vec3> bushWorldCenters;
    if (!desc.enableBushes)
    {
        return bushWorldCenters;
    }

    const std::array<std::uint32_t, 3> bushMeshes = {
        buildRosetteBushMesh(scene, desc.bushSeed ^ 0x101u),
        buildPinnateFernMesh(scene, desc.bushSeed ^ 0x203u),
        buildStemmedBroadleafMesh(scene, desc.bushSeed ^ 0x307u),
    };

    const auto cellBuckets = buildObstacleBuckets(map, obstacles);
    std::vector<glm::vec2> placedCenters;
    std::mt19937 rng(desc.bushSeed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> yawDist(0.0f, kTwoPi);
    const float cell = map.cellSize;

    for (int y = 1; y < map.size.y - 1; ++y)
    {
        for (int x = 1; x < map.size.x - 1; ++x)
        {
            if (map.getCell({x, y}) != CellKind::Courtyard)
            {
                continue;
            }

            const bool wallW = map.getCell({x - 1, y}) == CellKind::Wall;
            const bool wallE = map.getCell({x + 1, y}) == CellKind::Wall;
            const bool wallN = map.getCell({x, y - 1}) == CellKind::Wall;
            const bool wallS = map.getCell({x, y + 1}) == CellKind::Wall;
            const bool nearWall = wallW || wallE || wallN || wallS;

            const float chance = nearWall ? desc.bushEdgeChance : desc.bushInteriorChance;
            if (unit(rng) > chance)
            {
                continue;
            }

            const float baseX = static_cast<float>(x) * cell;
            const float baseZ = static_cast<float>(y) * cell;
            const float minX = wallW ? 0.62f : 0.35f;
            const float maxX = wallE ? (cell - 0.62f) : (cell - 0.35f);
            const float minZ = wallN ? 0.62f : 0.35f;
            const float maxZ = wallS ? (cell - 0.62f) : (cell - 0.35f);

            float anchorU = unit(rng);
            float anchorV = unit(rng);
            if (wallW && !wallE) anchorU *= 0.42f;
            else if (wallE && !wallW) anchorU = 1.0f - anchorU * 0.42f;
            if (wallN && !wallS) anchorV *= 0.42f;
            else if (wallS && !wallN) anchorV = 1.0f - anchorV * 0.42f;

            const glm::vec2 clusterAnchor(baseX + glm::mix(minX, maxX, anchorU), baseZ + glm::mix(minZ, maxZ, anchorV));
            const int clusterCount = (unit(rng) < 0.55f) ? 2 : 1;

            for (int c = 0; c < clusterCount; ++c)
            {
                const float spread = (c == 0) ? 0.0f : (0.52f + 0.25f * unit(rng));
                const float dirA = yawDist(rng);
                const glm::vec2 cand = (c == 0) ? clusterAnchor : (clusterAnchor + glm::vec2(std::cos(dirA) * spread, std::sin(dirA) * spread));

                const glm::ivec2 cCell = map.worldToCell(glm::vec3(cand.x, 0.0f, cand.y));
                if (map.getCell(cCell) != CellKind::Courtyard || !isGameplayClear(map, cand, 0.45f))
                {
                    continue;
                }
                if (evalPuddleBasin(cand) >= 0.57f)
                {
                    continue;
                }

                bool overlaps = minSignedDistanceToObstacles(cand, cellBuckets, obstacles, map) < -0.04f;
                for (const glm::vec2 &prev : placedCenters)
                {
                    const glm::vec2 d = cand - prev;
                    if (glm::dot(d, d) < 0.48f * 0.48f)
                    {
                        overlaps = true;
                        break;
                    }
                }
                if (overlaps)
                {
                    continue;
                }

                const float scaleXZ = (c == 0) ? (0.95f + 0.32f * unit(rng)) : (0.68f + 0.24f * unit(rng));
                const float scaleY = scaleXZ * (0.90f + 0.24f * unit(rng));
                const float yaw = yawDist(rng);
                const std::uint32_t meshId = bushMeshes[static_cast<std::size_t>((x * 7 + y * 13 + c) % bushMeshes.size())];

                glm::mat4 model(1.0f);
                model = glm::translate(model, glm::vec3(cand.x, 0.0f, cand.y));
                model = glm::rotate(model, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
                model = glm::scale(model, glm::vec3(scaleXZ, scaleY, scaleXZ));
                scene.addObject(meshId, bushMaterialId, model, render::kObjectVisibleFlag | render::kObjectWindSwayFlag);

                placedCenters.push_back(cand);
                bushWorldCenters.emplace_back(cand.x, 0.0f, cand.y);
                obstacles.push_back(FloorObstacleObb::fromYaw(cand, yaw, glm::vec2(0.24f * scaleXZ), false));
            }
        }
    }

    return bushWorldCenters;
}

std::vector<render::GrassInstance> generateMazeGrass(const MazeMapData &map, const std::vector<FloorObstacleObb> &obstacles, const FoliageGenDesc &desc)
{
    std::vector<render::GrassInstance> out;
    if (!desc.enableGrass)
    {
        return out;
    }

    const auto cellBuckets = buildObstacleBuckets(map, obstacles);
    std::mt19937 rng(desc.grassSeed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> yawDist(0.0f, kTwoPi);

    const float cell = map.cellSize;
    out.reserve(map.cells.size() * 28u);

    for (int y = 1; y < map.size.y - 1; ++y)
    {
        for (int x = 1; x < map.size.x - 1; ++x)
        {
            const CellKind kind = map.getCell({x, y});
            if (!map.isWalkable({x, y}))
            {
                continue;
            }

            const bool isCourtyard = (kind == CellKind::Courtyard);
            const int gridN = std::max(2, isCourtyard ? desc.grassGridCourtyard : desc.grassGridCorridor);
            const float marginW = (map.getCell({x - 1, y}) == CellKind::Wall) ? 0.36f : 0.04f;
            const float marginE = (map.getCell({x + 1, y}) == CellKind::Wall) ? 0.36f : 0.04f;
            const float marginN = (map.getCell({x, y - 1}) == CellKind::Wall) ? 0.36f : 0.04f;
            const float marginS = (map.getCell({x, y + 1}) == CellKind::Wall) ? 0.36f : 0.04f;
            const float spanX = cell - marginW - marginE;
            const float spanZ = cell - marginN - marginS;
            if (spanX <= 0.2f || spanZ <= 0.2f)
            {
                continue;
            }

            const float stepX = spanX / static_cast<float>(gridN);
            const float stepZ = spanZ / static_cast<float>(gridN);
            const float originX = static_cast<float>(x) * cell + marginW;
            const float originZ = static_cast<float>(y) * cell + marginN;

            for (int gz = 0; gz < gridN; ++gz)
            {
                for (int gx = 0; gx < gridN; ++gx)
                {
                    if (!isCourtyard && unit(rng) < 0.22f)
                    {
                        continue;
                    }

                    const float px = originX + (static_cast<float>(gx) + 0.15f + 0.70f * unit(rng)) * stepX;
                    const float pz = originZ + (static_cast<float>(gz) + 0.15f + 0.70f * unit(rng)) * stepZ;
                    const glm::vec2 posXZ(px, pz);

                    if (!isGameplayClear(map, posXZ, 0.10f))
                    {
                        continue;
                    }

                    const float puddleBasin = evalPuddleBasin(posXZ);
                    if (puddleBasin >= 0.56f)
                    {
                        continue;
                    }

                    const float obstacleDist = minSignedDistanceToObstacles(posXZ, cellBuckets, obstacles, map);
                    if (obstacleDist < desc.grassMinObstacleClearance)
                    {
                        continue;
                    }

                    const float seamFactor = std::clamp((obstacleDist - desc.grassMinObstacleClearance) / 0.14f, 0.0f, 1.0f);
                    const float puddleShoreTaper = 1.0f - glm::smoothstep(0.50f, 0.56f, puddleBasin) * 0.45f;
                    const float widthScale = glm::mix(0.76f, 1.15f, seamFactor) * (0.85f + 0.30f * unit(rng));
                    const float heightScale = glm::mix(0.62f, 1.25f, seamFactor) * (0.80f + 0.40f * unit(rng)) * puddleShoreTaper * (isCourtyard ? 1.08f : 0.90f);

                    out.push_back(render::GrassInstance{
                        .posYaw = glm::vec4(px, 0.0f, pz, yawDist(rng)),
                        .scaleSeed = glm::vec4(widthScale, heightScale, unit(rng), 0.75f + 0.50f * unit(rng)),
                    });
                }
            }
        }
    }

    return out;
}

} // namespace game
