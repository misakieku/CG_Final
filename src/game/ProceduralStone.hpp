#pragma once

#include "game/render/MegaScene.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

namespace game
{

struct ConvexPolyhedron
{
    std::vector<std::vector<glm::vec3>> faces;
};

struct StoneMeshBuilder
{
    std::vector<render::SceneVertex> vertices;
    std::vector<std::uint32_t> indices;

    void appendPolyhedron(const ConvexPolyhedron &poly, const glm::mat4 &xform = glm::mat4(1.0f))
    {
        for (const std::vector<glm::vec3> &face : poly.faces)
        {
            if (face.size() < 3)
            {
                continue;
            }

            std::vector<glm::vec3> pts;
            pts.reserve(face.size());
            for (const glm::vec3 &p : face)
            {
                pts.push_back(glm::vec3(xform * glm::vec4(p, 1.0f)));
            }

            const glm::vec3 crossN = glm::cross(pts[1] - pts[0], pts[2] - pts[0]);
            const float lenSq = glm::dot(crossN, crossN);
            if (lenSq < 1e-10f)
            {
                continue;
            }
            const glm::vec3 normal = crossN / std::sqrt(lenSq);

            // Orthonormal basis for planar UV fallback when kObjectWorldUVFlag is off
            const glm::vec3 refAxis = (std::abs(normal.y) < 0.9f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 tangent = glm::normalize(glm::cross(refAxis, normal));
            const glm::vec3 bitangent = glm::cross(normal, tangent);

            const auto baseIdx = static_cast<std::uint32_t>(vertices.size());
            for (const glm::vec3 &p : pts)
            {
                const glm::vec2 uv(glm::dot(p, tangent) * 0.333333f, glm::dot(p, bitangent) * 0.333333f);
                vertices.push_back(render::SceneVertex{p, normal, uv});
            }

            for (std::size_t k = 1; k + 1 < pts.size(); ++k)
            {
                indices.push_back(baseIdx);
                indices.push_back(baseIdx + static_cast<std::uint32_t>(k));
                indices.push_back(baseIdx + static_cast<std::uint32_t>(k + 1));
            }
        }
    }

    void appendBox(glm::vec3 minP, glm::vec3 maxP)
    {
        appendPolyhedron(makeBoxPolyhedron(minP, maxP));
    }

    [[nodiscard]] static ConvexPolyhedron makeBoxPolyhedron(glm::vec3 minP, glm::vec3 maxP)
    {
        const glm::vec3 p000(minP.x, minP.y, minP.z);
        const glm::vec3 p100(maxP.x, minP.y, minP.z);
        const glm::vec3 p110(maxP.x, maxP.y, minP.z);
        const glm::vec3 p010(minP.x, maxP.y, minP.z);
        const glm::vec3 p001(minP.x, minP.y, maxP.z);
        const glm::vec3 p101(maxP.x, minP.y, maxP.z);
        const glm::vec3 p111(maxP.x, maxP.y, maxP.z);
        const glm::vec3 p011(minP.x, maxP.y, maxP.z);

        return makeOrientedHexahedron(p000, p100, p110, p010, p001, p101, p111, p011);
    }

    [[nodiscard]] static ConvexPolyhedron makeOrientedHexahedron(glm::vec3 p000, glm::vec3 p100, glm::vec3 p110, glm::vec3 p010, glm::vec3 p001, glm::vec3 p101, glm::vec3 p111, glm::vec3 p011)
    {
        ConvexPolyhedron poly;
        poly.faces = {
            {p000, p010, p110, p100}, // -Z
            {p001, p101, p111, p011}, // +Z
            {p000, p001, p011, p010}, // -X
            {p100, p110, p111, p101}, // +X
            {p000, p100, p101, p001}, // -Y
            {p010, p011, p111, p110}, // +Y
        };

        const glm::vec3 center = (p000 + p100 + p110 + p010 + p001 + p101 + p111 + p011) * 0.125f;
        for (std::vector<glm::vec3> &face : poly.faces)
        {
            const glm::vec3 n = glm::cross(face[1] - face[0], face[2] - face[0]);
            if (glm::dot(n, center - face[0]) > 0.0f)
            {
                std::reverse(face.begin(), face.end());
            }
        }
        return poly;
    }

    [[nodiscard]] static ConvexPolyhedron makeExtrudedPolygon(std::span<const glm::vec2> loopXY, float zMin, float zMax)
    {
        ConvexPolyhedron poly;
        if (loopXY.size() < 3)
        {
            return poly;
        }

        std::vector<glm::vec3> frontFace;
        std::vector<glm::vec3> backFace;
        frontFace.reserve(loopXY.size());
        backFace.reserve(loopXY.size());

        glm::vec3 center(0.0f);
        for (const glm::vec2 &p : loopXY)
        {
            frontFace.emplace_back(p.x, p.y, zMax);
            backFace.emplace_back(p.x, p.y, zMin);
            center += glm::vec3(p.x, p.y, (zMin + zMax) * 0.5f);
        }
        center /= static_cast<float>(loopXY.size());

        poly.faces.push_back(frontFace);
        poly.faces.push_back(backFace);
        for (std::size_t i = 0; i < loopXY.size(); ++i)
        {
            const std::size_t j = (i + 1) % loopXY.size();
            poly.faces.push_back({backFace[i], backFace[j], frontFace[j], frontFace[i]});
        }

        for (std::vector<glm::vec3> &face : poly.faces)
        {
            const glm::vec3 n = glm::cross(face[1] - face[0], face[2] - face[0]);
            if (glm::dot(n, center - face[0]) > 0.0f)
            {
                std::reverse(face.begin(), face.end());
            }
        }
        return poly;
    }

    // Half-space Sutherland-Hodgman clip keeping dot(p - planePoint, outwardNormal) <= 0
    static void clipWithPlane(ConvexPolyhedron &poly, glm::vec3 planePoint, glm::vec3 outwardNormal)
    {
        const float nLenSq = glm::dot(outwardNormal, outwardNormal);
        if (nLenSq < 1e-8f || poly.faces.empty())
        {
            return;
        }
        const glm::vec3 n = outwardNormal / std::sqrt(nLenSq);
        constexpr float kEps = 1e-5f;

        std::vector<std::vector<glm::vec3>> clippedFaces;
        clippedFaces.reserve(poly.faces.size() + 1);
        std::vector<glm::vec3> capPts;

        auto pushUniqueCap = [&](glm::vec3 pt)
        {
            for (const glm::vec3 &existing : capPts)
            {
                const glm::vec3 d = pt - existing;
                if (glm::dot(d, d) < 1e-9f)
                {
                    return;
                }
            }
            capPts.push_back(pt);
        };

        for (const std::vector<glm::vec3> &face : poly.faces)
        {
            if (face.size() < 3)
            {
                continue;
            }
            std::vector<glm::vec3> outPoly;
            outPoly.reserve(face.size() + 2);

            for (std::size_t i = 0; i < face.size(); ++i)
            {
                const glm::vec3 &a = face[i];
                const glm::vec3 &b = face[(i + 1) % face.size()];
                const float da = glm::dot(a - planePoint, n);
                const float db = glm::dot(b - planePoint, n);
                const bool aIn = (da <= kEps);
                const bool bIn = (db <= kEps);

                if (aIn && bIn)
                {
                    if (std::abs(db) <= kEps)
                    {
                        pushUniqueCap(b);
                    }
                    outPoly.push_back(b);
                }
                else if (aIn && !bIn)
                {
                    const float t = da / (da - db);
                    const glm::vec3 inter = a + (b - a) * std::clamp(t, 0.0f, 1.0f);
                    pushUniqueCap(inter);
                    outPoly.push_back(inter);
                }
                else if (!aIn && bIn)
                {
                    const float t = da / (da - db);
                    const glm::vec3 inter = a + (b - a) * std::clamp(t, 0.0f, 1.0f);
                    pushUniqueCap(inter);
                    outPoly.push_back(inter);
                    if (std::abs(db) <= kEps)
                    {
                        pushUniqueCap(b);
                    }
                    outPoly.push_back(b);
                }
            }

            // Deduplicate consecutive vertices along nearly coincident clip crossings
            std::vector<glm::vec3> cleanPoly;
            cleanPoly.reserve(outPoly.size());
            for (const glm::vec3 &p : outPoly)
            {
                if (cleanPoly.empty() || glm::dot(p - cleanPoly.back(), p - cleanPoly.back()) > 1e-9f)
                {
                    cleanPoly.push_back(p);
                }
            }
            if (cleanPoly.size() >= 3 && glm::dot(cleanPoly.front() - cleanPoly.back(), cleanPoly.front() - cleanPoly.back()) <= 1e-9f)
            {
                cleanPoly.pop_back();
            }
            if (cleanPoly.size() >= 3)
            {
                clippedFaces.push_back(std::move(cleanPoly));
            }
        }

        if (capPts.size() >= 3)
        {
            glm::vec3 centroid(0.0f);
            for (const glm::vec3 &p : capPts)
            {
                centroid += p;
            }
            centroid /= static_cast<float>(capPts.size());

            const glm::vec3 refAxis = (std::abs(n.y) < 0.9f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 t = glm::normalize(glm::cross(refAxis, n));
            const glm::vec3 b = glm::cross(n, t);

            std::sort(capPts.begin(), capPts.end(), [&](const glm::vec3 &u, const glm::vec3 &v)
            {
                const float au = std::atan2(glm::dot(u - centroid, b), glm::dot(u - centroid, t));
                const float av = std::atan2(glm::dot(v - centroid, b), glm::dot(v - centroid, t));
                return au < av;
            });
            clippedFaces.push_back(std::move(capPts));
        }

        poly.faces = std::move(clippedFaces);
    }
};

/// Sculpt angled stone bevels and ZBrush-style corner/edge fracture notches off a convex stone block.
inline void sculptChippedStone(ConvexPolyhedron &poly, glm::vec3 center, glm::vec3 halfExt, std::mt19937 &rng, bool clipFrontZOnly, float notchScale = 1.0f)
{
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float minSide = std::min({halfExt.x, halfExt.y, halfExt.z});
    const float bevelDepth = minSide * (0.14f + 0.12f * unit(rng));

    // Crisp 45-degree front edge bevels so torchlight catches stone rims
    const std::array<glm::vec3, 4> edgeNormals = {
        glm::normalize(glm::vec3(-1.0f, 0.0f, 1.0f)),
        glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f)),
        glm::normalize(glm::vec3(0.0f, -1.0f, 1.0f)),
        glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f)),
    };
    const std::array<glm::vec3, 4> edgeCorners = {
        glm::vec3(-halfExt.x, 0.0f, halfExt.z),
        glm::vec3(halfExt.x, 0.0f, halfExt.z),
        glm::vec3(0.0f, -halfExt.y, halfExt.z),
        glm::vec3(0.0f, halfExt.y, halfExt.z),
    };
    for (std::size_t i = 0; i < 4; ++i)
    {
        const glm::vec3 cutPt = center + edgeCorners[i] - edgeNormals[i] * (bevelDepth * (0.7f + 0.6f * unit(rng)));
        StoneMeshBuilder::clipWithPlane(poly, cutPt, edgeNormals[i]);
    }

    if (!clipFrontZOnly)
    {
        const std::array<glm::vec3, 4> backEdgeNormals = {
            glm::normalize(glm::vec3(-1.0f, 0.0f, -1.0f)),
            glm::normalize(glm::vec3(1.0f, 0.0f, -1.0f)),
            glm::normalize(glm::vec3(0.0f, -1.0f, -1.0f)),
            glm::normalize(glm::vec3(0.0f, 1.0f, -1.0f)),
        };
        const std::array<glm::vec3, 4> backEdgeCorners = {
            glm::vec3(-halfExt.x, 0.0f, -halfExt.z),
            glm::vec3(halfExt.x, 0.0f, -halfExt.z),
            glm::vec3(0.0f, -halfExt.y, -halfExt.z),
            glm::vec3(0.0f, halfExt.y, -halfExt.z),
        };
        for (std::size_t i = 0; i < 4; ++i)
        {
            const glm::vec3 cutPt = center + backEdgeCorners[i] - backEdgeNormals[i] * (bevelDepth * (0.7f + 0.5f * unit(rng)));
            StoneMeshBuilder::clipWithPlane(poly, cutPt, backEdgeNormals[i]);
        }
    }

    // 2 to 4 asymmetric deep angular fracture cuts slicing corners or side edges
    const int notchCount = 2 + static_cast<int>(unit(rng) * 3.0f);
    for (int n = 0; n < notchCount; ++n)
    {
        const float sx = (unit(rng) < 0.5f) ? -1.0f : 1.0f;
        const float sy = (unit(rng) < 0.5f) ? -1.0f : 1.0f;
        const float sz = clipFrontZOnly ? 1.0f : ((unit(rng) < 0.65f) ? 1.0f : -1.0f);

        const glm::vec3 corner = center + glm::vec3(sx * halfExt.x, sy * halfExt.y, sz * halfExt.z);
        const glm::vec3 cutNormal = glm::normalize(glm::vec3(
            sx * (0.45f + 0.90f * unit(rng)),
            sy * (0.45f + 0.90f * unit(rng)),
            sz * (0.35f + 0.70f * unit(rng))));
        const float sliceDepth = minSide * (0.28f + 0.48f * unit(rng)) * notchScale;
        StoneMeshBuilder::clipWithPlane(poly, corner - cutNormal * sliceDepth, cutNormal);
    }
}

/// Builds a 3.0m x wallHeight x 3.0m procedural wall mesh: recessed mortar core + 4 faces of plane-clipped 3D stone blocks.
[[nodiscard]] inline std::uint32_t buildChippedWallVariantMesh(render::MegaScene &scene, float cellSize, float wallHeight, std::uint32_t seed)
{
    StoneMeshBuilder mb;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    const float halfCell = cellSize * 0.5f;
    const float coreHalf = halfCell - 0.065f;
    mb.appendBox(glm::vec3(-coreHalf, 0.0f, -coreHalf), glm::vec3(coreHalf, wallHeight, coreHalf));

    constexpr int kRows = 6;
    const float rowH = wallHeight / static_cast<float>(kRows);
    constexpr float kSideGap = 0.014f;
    constexpr float kRowGap = 0.012f;

    for (int faceIdx = 0; faceIdx < 4; ++faceIdx)
    {
        const glm::mat4 faceRot = glm::rotate(glm::mat4(1.0f), static_cast<float>(faceIdx) * 1.5707963f, glm::vec3(0.0f, 1.0f, 0.0f));
        const float faceMaxSpan = cellSize - 0.02f;

        for (int row = 0; row < kRows; ++row)
        {
            const float y0 = static_cast<float>(row) * rowH + kRowGap * 0.5f;
            const float y1 = static_cast<float>(row + 1) * rowH - kRowGap * 0.5f;

            // Varied course subdivision (2 to 4 uneven wide/medium blocks per 3m course)
            const int cols = (row % 2 == 0) ? (3 + (unit(rng) < 0.35f ? 1 : 0)) : (3 + (unit(rng) < 0.55f ? 1 : 0));
            std::vector<float> weights(static_cast<std::size_t>(cols));
            float wSum = 0.0f;
            for (int c = 0; c < cols; ++c)
            {
                weights[static_cast<std::size_t>(c)] = 0.65f + 0.85f * unit(rng);
                wSum += weights[static_cast<std::size_t>(c)];
            }

            float cursorX = -faceMaxSpan * 0.5f;
            for (int c = 0; c < cols; ++c)
            {
                const float segW = (weights[static_cast<std::size_t>(c)] / wSum) * faceMaxSpan;
                const float x0 = cursorX + kSideGap * 0.5f;
                const float x1 = cursorX + segW - kSideGap * 0.5f;
                cursorX += segW;

                auto emitStone = [&](float sx0, float sx1, float sy0, float sy1)
                {
                    // Occasional deep recessed pocket simulating a weathered/missing stone block
                    const float roll = unit(rng);
                    if (roll < 0.07f)
                    {
                        return;
                    }
                    const float zFront = (roll < 0.18f)
                                             ? (halfCell - 0.030f + 0.015f * unit(rng))
                                             : (halfCell - 0.004f + 0.058f * unit(rng));
                    const float zBack = halfCell - 0.085f;

                    const glm::vec3 sCenter((sx0 + sx1) * 0.5f, (sy0 + sy1) * 0.5f, (zBack + zFront) * 0.5f);
                    const glm::vec3 sHalf((sx1 - sx0) * 0.5f, (sy1 - sy0) * 0.5f, (zFront - zBack) * 0.5f);
                    ConvexPolyhedron poly = StoneMeshBuilder::makeBoxPolyhedron(sCenter - sHalf, sCenter + sHalf);
                    sculptChippedStone(poly, sCenter, sHalf, rng, true, 1.05f);

                    const glm::mat4 tilt = glm::translate(faceRot, sCenter) *
                                           glm::rotate(glm::mat4(1.0f), (unit(rng) - 0.5f) * 0.035f, glm::vec3(1.0f, 0.0f, 0.0f)) *
                                           glm::rotate(glm::mat4(1.0f), (unit(rng) - 0.5f) * 0.030f, glm::vec3(0.0f, 1.0f, 0.0f)) *
                                           glm::translate(glm::mat4(1.0f), -sCenter);
                    mb.appendPolyhedron(poly, tilt);
                };

                // Split occasional tall blocks horizontally into twin thinner blocks
                if (unit(rng) < 0.22f)
                {
                    const float midY = glm::mix(y0, y1, 0.42f + 0.16f * unit(rng));
                    emitStone(x0, x1, y0, midY - kRowGap * 0.4f);
                    emitStone(x0, x1, midY + kRowGap * 0.4f, y1);
                }
                else
                {
                    emitStone(x0, x1, y0, y1);
                }
            }
        }
    }

    return scene.registerMesh(mb.vertices, mb.indices);
}

/// Builds a segmented chipped stone trim beam centered at origin spanning [-spanX/2, +spanX/2].
[[nodiscard]] inline std::uint32_t buildChippedTrimMesh(render::MegaScene &scene, float spanX, float heightY, float depthZ, int segments, std::uint32_t seed)
{
    StoneMeshBuilder mb;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    const float gap = 0.018f;
    const float segW = spanX / static_cast<float>(std::max(1, segments));
    for (int i = 0; i < segments; ++i)
    {
        const float x0 = -spanX * 0.5f + static_cast<float>(i) * segW + gap * 0.5f;
        const float x1 = x0 + segW - gap;
        const float hJit = heightY * (0.94f + 0.10f * unit(rng));
        const float dJit = depthZ * (0.94f + 0.12f * unit(rng));
        const glm::vec3 c((x0 + x1) * 0.5f, 0.0f, (dJit - depthZ) * 0.35f);
        const glm::vec3 half((x1 - x0) * 0.5f, hJit * 0.5f, dJit * 0.5f);

        ConvexPolyhedron poly = StoneMeshBuilder::makeBoxPolyhedron(c - half, c + half);
        sculptChippedStone(poly, c, half, rng, false, 0.85f);
        mb.appendPolyhedron(poly);
    }

    return scene.registerMesh(mb.vertices, mb.indices);
}

/// Builds a radial segmented stone archway spanning cellSize with wall pilasters and chipped wedge voussoirs.
[[nodiscard]] inline std::uint32_t buildSegmentedStoneArchMesh(render::MegaScene &scene, float cellSize, float wallHeight, std::uint32_t seed)
{
    StoneMeshBuilder mb;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    const float halfSpan = cellSize * 0.5f;
    const float innerR = halfSpan - 0.36f;       // ~1.14m inner clear arch radius (2.28m wide opening)
    const float outerR = halfSpan - 0.04f;       // ~1.46m outer arch ring extrados
    const float springerY = std::max(1.85f, wallHeight - outerR - 0.06f);
    const float archDepth = 0.24f;               // half-depth -> 0.48m thick arch rib

    // Side stone pilasters & stepped impost capitals supporting the arch springers
    for (const float side : {-1.0f, 1.0f})
    {
        const float wallX = side * (halfSpan + 0.03f);
        const float pierInnerX = side * innerR;
        const float xMin = std::min(wallX, pierInnerX);
        const float xMax = std::max(wallX, pierInnerX);

        constexpr int kPierCourses = 4;
        const float courseH = (springerY - 0.18f) / static_cast<float>(kPierCourses);
        for (int c = 0; c < kPierCourses; ++c)
        {
            const float y0 = static_cast<float>(c) * courseH + 0.008f;
            const float y1 = static_cast<float>(c + 1) * courseH - 0.008f;
            const glm::vec3 center((xMin + xMax) * 0.5f, (y0 + y1) * 0.5f, 0.0f);
            const glm::vec3 half((xMax - xMin) * 0.5f, (y1 - y0) * 0.5f, archDepth * 0.86f);
            ConvexPolyhedron pierBlock = StoneMeshBuilder::makeBoxPolyhedron(center - half, center + half);
            sculptChippedStone(pierBlock, center, half, rng, false, 0.75f);
            mb.appendPolyhedron(pierBlock);
        }

        // Flared impost capital block beneath the arch springer
        const float capW = (xMax - xMin) + 0.10f;
        const glm::vec3 capCenter(side * (halfSpan - capW * 0.45f), springerY - 0.09f, 0.0f);
        const glm::vec3 capHalf(capW * 0.5f, 0.09f, archDepth * 1.05f);
        ConvexPolyhedron capPoly = StoneMeshBuilder::makeBoxPolyhedron(capCenter - capHalf, capCenter + capHalf);
        sculptChippedStone(capPoly, capCenter, capHalf, rng, false, 0.65f);
        mb.appendPolyhedron(capPoly);
    }

    // 9 tightly fitted radial stone voussoirs (+ proud keystone at center i == 4)
    constexpr int kVoussoirs = 9;
    constexpr float kPi = 3.14159265f;
    const float angleStep = kPi / static_cast<float>(kVoussoirs);
    constexpr float kAngleGap = 0.005f;

    for (int i = 0; i < kVoussoirs; ++i)
    {
        const bool isKeystone = (i == kVoussoirs / 2);
        const float a0 = static_cast<float>(i) * angleStep + kAngleGap;
        const float a1 = static_cast<float>(i + 1) * angleStep - kAngleGap;

        const float rIn = isKeystone ? (innerR - 0.045f) : (innerR - 0.012f * unit(rng));
        const float rOut = isKeystone ? (outerR + 0.040f) : (outerR + 0.015f * unit(rng));
        const float zH = isKeystone ? (archDepth * 1.12f) : (archDepth * (0.95f + 0.10f * unit(rng)));

        auto arcPt = [&](float r, float a, float z) -> glm::vec3
        {
            return glm::vec3(std::cos(a) * r, springerY + std::sin(a) * r, z);
        };

        const glm::vec3 p000 = arcPt(rIn, a0, -zH);
        const glm::vec3 p100 = arcPt(rIn, a1, -zH);
        const glm::vec3 p110 = arcPt(rOut, a1, -zH);
        const glm::vec3 p010 = arcPt(rOut, a0, -zH);
        const glm::vec3 p001 = arcPt(rIn, a0, zH);
        const glm::vec3 p101 = arcPt(rIn, a1, zH);
        const glm::vec3 p111 = arcPt(rOut, a1, zH);
        const glm::vec3 p011 = arcPt(rOut, a0, zH);

        ConvexPolyhedron voussoir = StoneMeshBuilder::makeOrientedHexahedron(p000, p100, p110, p010, p001, p101, p111, p011);
        const glm::vec3 vCenter = (p000 + p100 + p110 + p010 + p001 + p101 + p111 + p011) * 0.125f;
        const glm::vec3 vHalf((rOut - rIn) * 0.45f, (rOut - rIn) * 0.45f, zH * 0.90f);
        sculptChippedStone(voussoir, vCenter, vHalf, rng, false, isKeystone ? 0.25f : 0.35f);
        mb.appendPolyhedron(voussoir);
    }

    // Flush arc-fitted radial spandrel wedges filling every gap from outerR out to the side walls & ceiling
    const float xBound = halfSpan + 0.04f;
    const float yBound = wallHeight + 0.04f;
    const float rSpandrelIn = outerR - 0.045f; // overlaps 4.5cm behind the proud arch ring so no slit can appear
    const float aRightCorner = std::atan2(yBound - springerY, xBound);
    const float aLeftCorner = kPi - aRightCorner;

    auto rayToFrame = [&](float a) -> glm::vec2
    {
        const float ca = std::cos(a);
        const float sa = std::sin(a);
        const float tx = (std::abs(ca) > 1e-4f) ? (xBound / std::abs(ca)) : 1e6f;
        const float ty = (sa > 1e-4f) ? ((yBound - springerY) / sa) : 1e6f;
        const float r = std::max(rSpandrelIn + 0.02f, std::min(tx, ty));
        return glm::vec2(ca * r, springerY + sa * r);
    };

    for (int i = 0; i < kVoussoirs; ++i)
    {
        const float a0 = static_cast<float>(i) * angleStep + 0.002f;
        const float a1 = static_cast<float>(i + 1) * angleStep - 0.002f;

        std::vector<glm::vec2> loop;
        loop.reserve(5);
        loop.emplace_back(std::cos(a0) * rSpandrelIn, springerY + std::sin(a0) * rSpandrelIn);
        loop.emplace_back(std::cos(a1) * rSpandrelIn, springerY + std::sin(a1) * rSpandrelIn);
        loop.push_back(rayToFrame(a1));
        if (a0 < aLeftCorner && aLeftCorner < a1)
        {
            loop.emplace_back(-xBound, yBound);
        }
        if (a0 < aRightCorner && aRightCorner < a1)
        {
            loop.emplace_back(xBound, yBound);
        }
        loop.push_back(rayToFrame(a0));

        const float spZ = archDepth * (0.74f + 0.06f * unit(rng));
        ConvexPolyhedron sp = StoneMeshBuilder::makeExtrudedPolygon(loop, -spZ, spZ);
        mb.appendPolyhedron(sp);
    }

    return scene.registerMesh(mb.vertices, mb.indices);
}

/// Sculpts horizontal bevels and bold XZ corner fractures on wide/flat stone slabs (pavers, ceiling blocks, capital tiers).
inline void sculptChippedSlab(ConvexPolyhedron &poly, glm::vec3 center, glm::vec3 halfExt, std::mt19937 &rng, float visibleYSign, float notchScale = 1.0f)
{
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float minXZ = std::min(halfExt.x, halfExt.z);
    const float edgeChamferY = std::min(halfExt.y * 0.42f, minXZ * 0.16f);

    // 4 upper or lower horizontal rim chamfers around the visible slab face
    const std::array<glm::vec3, 4> rimNormals = {
        glm::normalize(glm::vec3(-1.0f, visibleYSign, 0.0f)),
        glm::normalize(glm::vec3(1.0f, visibleYSign, 0.0f)),
        glm::normalize(glm::vec3(0.0f, visibleYSign, -1.0f)),
        glm::normalize(glm::vec3(0.0f, visibleYSign, 1.0f)),
    };
    const std::array<glm::vec3, 4> rimEdges = {
        glm::vec3(-halfExt.x, visibleYSign * halfExt.y, 0.0f),
        glm::vec3(halfExt.x, visibleYSign * halfExt.y, 0.0f),
        glm::vec3(0.0f, visibleYSign * halfExt.y, -halfExt.z),
        glm::vec3(0.0f, visibleYSign * halfExt.y, halfExt.z),
    };
    for (std::size_t i = 0; i < 4; ++i)
    {
        const glm::vec3 cutPt = center + rimEdges[i] - rimNormals[i] * (edgeChamferY * (0.8f + 0.5f * unit(rng)));
        StoneMeshBuilder::clipWithPlane(poly, cutPt, rimNormals[i]);
    }

    // 2 to 4 vertical/angled corner fractures biting cleanly into the XZ silhouette
    const int cuts = 2 + static_cast<int>(unit(rng) * 3.0f);
    for (int k = 0; k < cuts; ++k)
    {
        const float sx = (unit(rng) < 0.5f) ? -1.0f : 1.0f;
        const float sz = (unit(rng) < 0.5f) ? -1.0f : 1.0f;
        const glm::vec3 corner = center + glm::vec3(sx * halfExt.x, visibleYSign * halfExt.y, sz * halfExt.z);
        const glm::vec3 cutNormal = glm::normalize(glm::vec3(
            sx * (0.45f + 0.85f * unit(rng)),
            visibleYSign * (0.10f + 0.35f * unit(rng)),
            sz * (0.45f + 0.85f * unit(rng))));
        const float sliceDepth = minXZ * (0.16f + 0.24f * unit(rng)) * notchScale;
        StoneMeshBuilder::clipWithPlane(poly, corner - cutNormal * sliceDepth, cutNormal);
    }
}

/// Builds a ceiling slab variant centered at (0, 0, 0) where y = 0 is the underside at wallHeight.
[[nodiscard]] inline std::uint32_t buildChippedCeilingVariantMesh(render::MegaScene &scene, float cellSize, std::uint32_t seed)
{
    StoneMeshBuilder mb;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    // Solid backing slab overlapping neighboring wall tops by 12cm to prevent light leaks
    const float backHalf = cellSize * 0.5f + 0.12f;
    mb.appendBox(glm::vec3(-backHalf, 0.03f, -backHalf), glm::vec3(backHalf, 0.30f, backHalf));

    constexpr int kRows = 4;
    constexpr int kCols = 3;
    const float spanXZ = cellSize - 0.02f;
    const float rowD = spanXZ / static_cast<float>(kRows);
    constexpr float kGap = 0.014f;

    for (int r = 0; r < kRows; ++r)
    {
        const float z0 = -spanXZ * 0.5f + static_cast<float>(r) * rowD + kGap * 0.5f;
        const float z1 = z0 + rowD - kGap;
        std::array<float, kCols> weights{};
        float wSum = 0.0f;
        for (int c = 0; c < kCols; ++c)
        {
            weights[static_cast<std::size_t>(c)] = 0.70f + 0.75f * unit(rng);
            wSum += weights[static_cast<std::size_t>(c)];
        }

        float curX = -spanXZ * 0.5f;
        for (int c = 0; c < kCols; ++c)
        {
            const float segW = (weights[static_cast<std::size_t>(c)] / wSum) * spanXZ;
            const float x0 = curX + kGap * 0.5f;
            const float x1 = curX + segW - kGap * 0.5f;
            curX += segW;

            if (unit(rng) < 0.06f)
            {
                continue; // Occasional recessed ceiling coffer pocket
            }

            const float yBot = -0.045f + 0.055f * unit(rng);
            const float yTop = 0.06f;
            const glm::vec3 center((x0 + x1) * 0.5f, (yBot + yTop) * 0.5f, (z0 + z1) * 0.5f);
            const glm::vec3 half((x1 - x0) * 0.5f, (yTop - yBot) * 0.5f, (z1 - z0) * 0.5f);
            ConvexPolyhedron slab = StoneMeshBuilder::makeBoxPolyhedron(center - half, center + half);
            sculptChippedSlab(slab, center, half, rng, -1.0f, 0.85f);

            const glm::mat4 tilt = glm::translate(glm::mat4(1.0f), center) *
                                   glm::rotate(glm::mat4(1.0f), (unit(rng) - 0.5f) * 0.030f, glm::vec3(1.0f, 0.0f, 0.0f)) *
                                   glm::rotate(glm::mat4(1.0f), (unit(rng) - 0.5f) * 0.030f, glm::vec3(0.0f, 0.0f, 1.0f)) *
                                   glm::translate(glm::mat4(1.0f), -center);
            mb.appendPolyhedron(slab, tilt);
        }
    }

    return scene.registerMesh(mb.vertices, mb.indices);
}

/// Builds a stacked chipped masonry pillar with a 3-tier Y-scaled stone base & capital.
[[nodiscard]] inline std::uint32_t buildChippedPillarMesh(render::MegaScene &scene, float wallHeight, bool steppedCapital, std::uint32_t seed)
{
    StoneMeshBuilder mb;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    auto emitTierBlock = [&](float yCenter, float width, float height, float notchScale)
    {
        const glm::vec3 half(width * 0.5f, height * 0.5f, width * 0.5f);
        const glm::vec3 c(0.0f, yCenter, 0.0f);
        ConvexPolyhedron poly = StoneMeshBuilder::makeBoxPolyhedron(c - half, c + half);
        sculptChippedSlab(poly, c, half, rng, 1.0f, notchScale);
        sculptChippedSlab(poly, c, half, rng, -1.0f, notchScale * 0.6f);
        const float yaw = (unit(rng) - 0.5f) * 0.045f;
        const glm::mat4 xform = glm::translate(glm::mat4(1.0f), c) *
                                glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                                glm::translate(glm::mat4(1.0f), -c);
        mb.appendPolyhedron(poly, xform);
    };

    float shaftY0 = 0.28f;
    float shaftY1 = wallHeight - 0.26f;

    if (steppedCapital)
    {
        // Two stepped base stone slabs + three flared Y-scaled temple capital slabs
        emitTierBlock(0.10f, 0.80f, 0.20f, 0.65f);
        emitTierBlock(0.27f, 0.66f, 0.14f, 0.55f);
        emitTierBlock(wallHeight - 0.40f, 0.66f, 0.14f, 0.55f);
        emitTierBlock(wallHeight - 0.24f, 0.78f, 0.17f, 0.65f);
        emitTierBlock(wallHeight - 0.08f, 0.88f, 0.17f, 0.70f);
        shaftY0 = 0.34f;
        shaftY1 = wallHeight - 0.47f;
    }
    else
    {
        emitTierBlock(0.13f, 0.78f, 0.26f, 0.65f);
        emitTierBlock(wallHeight - 0.13f, 0.80f, 0.26f, 0.65f);
    }

    // Stacked chipped stone shaft drums replacing the solid single box
    constexpr int kShaftCourses = 6;
    const float shaftH = std::max(0.5f, shaftY1 - shaftY0);
    const float courseH = shaftH / static_cast<float>(kShaftCourses);
    for (int i = 0; i < kShaftCourses; ++i)
    {
        const float y0 = shaftY0 + static_cast<float>(i) * courseH + 0.005f;
        const float y1 = shaftY0 + static_cast<float>(i + 1) * courseH - 0.005f;
        const float w = 0.53f + (unit(rng) - 0.5f) * 0.045f;
        const glm::vec3 c((unit(rng) - 0.5f) * 0.020f, (y0 + y1) * 0.5f, (unit(rng) - 0.5f) * 0.020f);
        const glm::vec3 half(w * 0.5f, (y1 - y0) * 0.5f, w * 0.5f);

        ConvexPolyhedron drum = StoneMeshBuilder::makeBoxPolyhedron(c - half, c + half);
        sculptChippedStone(drum, c, half, rng, false, 0.55f);
        const glm::mat4 rot = glm::translate(glm::mat4(1.0f), c) *
                              glm::rotate(glm::mat4(1.0f), (unit(rng) - 0.5f) * 0.06f, glm::vec3(0.0f, 1.0f, 0.0f)) *
                              glm::translate(glm::mat4(1.0f), -c);
        mb.appendPolyhedron(drum, rot);
    }

    return scene.registerMesh(mb.vertices, mb.indices);
}

/// Builds a low-profile chipped stone floor flagstone sitting cleanly on y = 0.
[[nodiscard]] inline std::uint32_t buildChippedPaverMesh(render::MegaScene &scene, float widthX, float heightY, float depthZ, std::uint32_t seed)
{
    StoneMeshBuilder mb;
    std::mt19937 rng(seed);
    const glm::vec3 center(0.0f, heightY * 0.45f, 0.0f);
    const glm::vec3 half(widthX * 0.5f, heightY * 0.5f, depthZ * 0.5f);
    ConvexPolyhedron poly = StoneMeshBuilder::makeBoxPolyhedron(center - half, center + half);
    sculptChippedSlab(poly, center, half, rng, 1.0f, 1.0f);
    mb.appendPolyhedron(poly);
    return scene.registerMesh(mb.vertices, mb.indices);
}

/// Builds a weathered timber floor plank aligned along local X with beveled long edges & splintered end cuts.
[[nodiscard]] inline std::uint32_t buildChippedWoodPlankMesh(render::MegaScene &scene, float lengthX, float thicknessY, float widthZ, std::uint32_t seed)
{
    StoneMeshBuilder mb;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    const glm::vec3 center(0.0f, thicknessY * 0.5f, 0.0f);
    const glm::vec3 half(lengthX * 0.5f, thicknessY * 0.5f, widthZ * 0.5f);
    ConvexPolyhedron poly = StoneMeshBuilder::makeBoxPolyhedron(center - half, center + half);

    // Subtle lengthwise edge chamfers so boards catch highlights without looking sharp-edged
    const float edgeChamfer = std::min(half.y * 0.36f, half.z * 0.14f);
    for (const float sz : {-1.0f, 1.0f})
    {
        const glm::vec3 n = glm::normalize(glm::vec3(0.0f, 1.0f, sz));
        StoneMeshBuilder::clipWithPlane(poly, center + glm::vec3(0.0f, half.y, sz * half.z) - n * edgeChamfer, n);
    }

    // Splintered diagonal cuts at the two plank ends (-X and +X)
    for (const float sx : {-1.0f, 1.0f})
    {
        const int cuts = 1 + (unit(rng) < 0.65f ? 1 : 0);
        for (int k = 0; k < cuts; ++k)
        {
            const float sz = (k == 0) ? ((unit(rng) < 0.5f) ? -1.0f : 1.0f) : -1.0f;
            const glm::vec3 endCorner = center + glm::vec3(sx * half.x, half.y, sz * half.z);
            const glm::vec3 cutN = glm::normalize(glm::vec3(
                sx * (0.65f + 0.55f * unit(rng)),
                0.08f + 0.22f * unit(rng),
                sz * (0.35f + 0.65f * unit(rng))));
            const float cutDepth = half.z * (0.28f + 0.45f * unit(rng));
            StoneMeshBuilder::clipWithPlane(poly, endCorner - cutN * cutDepth, cutN);
        }
    }

    mb.appendPolyhedron(poly);

    // Remap local UVs so wood grain runs strictly along board length (u = X, v = Z)
    for (render::SceneVertex &v : mb.vertices)
    {
        v.uv = glm::vec2(v.position.x * 0.75f + 0.5f, (v.position.z + v.position.y * 0.5f) * 2.2f + 0.5f);
    }
    return scene.registerMesh(mb.vertices, mb.indices);
}

} // namespace game
