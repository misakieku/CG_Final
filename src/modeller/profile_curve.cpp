#include "modeller/profile_curve.hpp"

#include <algorithm>
#include <cmath>

namespace modeller
{

glm::vec2 CubicBezierSegment::eval(float t) const noexcept
{
    const float u = 1.0f - t;
    const float u2 = u * u;
    const float t2 = t * t;
    return (u2 * u) * p[0] + (3.0f * u2 * t) * p[1] + (3.0f * u * t2) * p[2] + (t2 * t) * p[3];
}

glm::vec2 CubicBezierSegment::evalDerivative(float t) const noexcept
{
    const float u = 1.0f - t;
    return (3.0f * u * u) * (p[1] - p[0]) + (6.0f * u * t) * (p[2] - p[1]) + (3.0f * t * t) * (p[3] - p[2]);
}

std::vector<CubicBezierSegment> buildPiecewiseBezier(std::span<const glm::vec2> points, float smoothness)
{
    std::vector<CubicBezierSegment> segments;
    if (points.size() < 2)
    {
        return segments;
    }

    const int n = static_cast<int>(points.size());
    const float tension = std::clamp(smoothness, 0.0f, 1.0f);

    // Reflected virtual endpoints for smooth C1 tangents at curve ends
    auto getPoint = [&](int idx) -> glm::vec2
    {
        if (idx < 0)
        {
            return 2.0f * points[0] - points[1];
        }
        if (idx >= n)
        {
            return 2.0f * points[n - 1] - points[n - 2];
        }
        return points[static_cast<std::size_t>(idx)];
    };

    segments.reserve(static_cast<std::size_t>(n - 1));
    for (int i = 0; i < n - 1; ++i)
    {
        const glm::vec2 q0 = getPoint(i);
        const glm::vec2 q1 = getPoint(i + 1);
        const glm::vec2 t0 = tension * (getPoint(i + 1) - getPoint(i - 1));
        const glm::vec2 t1 = tension * (getPoint(i + 2) - getPoint(i));

        CubicBezierSegment seg;
        seg.p[0] = q0;
        seg.p[1] = q0 + t0 / 3.0f;
        seg.p[2] = q1 - t1 / 3.0f;
        seg.p[3] = q1;
        segments.push_back(seg);
    }

    return segments;
}

std::vector<ProfileSample> sampleProfileCurve(std::span<const CubicBezierSegment> segments, int samplesPerSegment)
{
    std::vector<ProfileSample> samples;
    if (segments.empty())
    {
        return samples;
    }

    const int steps = std::max(1, samplesPerSegment);
    samples.reserve(segments.size() * static_cast<std::size_t>(steps) + 1);

    for (std::size_t segIdx = 0; segIdx < segments.size(); ++segIdx)
    {
        const CubicBezierSegment &seg = segments[segIdx];
        const int startStep = (segIdx == 0) ? 0 : 1;
        for (int s = startStep; s <= steps; ++s)
        {
            const float t = static_cast<float>(s) / static_cast<float>(steps);
            glm::vec2 pos = seg.eval(t);
            pos.x = std::max(0.0f, pos.x);

            glm::vec2 tangent = seg.evalDerivative(t);
            if (glm::dot(tangent, tangent) < 1e-10f)
            {
                const float eps = 1e-3f;
                const float tClamped = std::clamp(t + (t < 0.5f ? eps : -eps), 0.0f, 1.0f);
                tangent = seg.evalDerivative(tClamped);
            }
            samples.push_back(ProfileSample{pos, tangent});
        }
    }

    return samples;
}

std::vector<glm::vec2> makePresetProfile(ProfilePreset preset)
{
    switch (preset)
    {
        case ProfilePreset::Chalice:
            return {
                {0.00f, -0.75f},
                {0.34f, -0.75f},
                {0.36f, -0.68f},
                {0.10f, -0.58f},
                {0.08f, -0.20f},
                {0.12f, -0.05f},
                {0.42f, 0.22f},
                {0.48f, 0.55f},
                {0.44f, 0.75f},
                {0.39f, 0.75f},
                {0.42f, 0.52f},
                {0.34f, 0.25f},
                {0.00f, 0.06f},
            };
        case ProfilePreset::Vase:
            return {
                {0.00f, -0.75f},
                {0.28f, -0.75f},
                {0.48f, -0.30f},
                {0.44f, 0.05f},
                {0.20f, 0.38f},
                {0.26f, 0.65f},
                {0.33f, 0.75f},
                {0.28f, 0.75f},
                {0.21f, 0.62f},
                {0.16f, 0.36f},
                {0.00f, -0.60f},
            };
        case ProfilePreset::Pillar:
            return {
                {0.00f, -0.80f},
                {0.42f, -0.80f},
                {0.42f, -0.68f},
                {0.30f, -0.62f},
                {0.28f, 0.62f},
                {0.42f, 0.68f},
                {0.42f, 0.80f},
                {0.00f, 0.80f},
            };
        case ProfilePreset::Candle:
            return {
                {0.00f, -0.70f},
                {0.28f, -0.70f},
                {0.28f, -0.60f},
                {0.12f, -0.52f},
                {0.15f, 0.35f},
                {0.16f, 0.42f},
                {0.08f, 0.42f},
                {0.07f, 0.34f},
                {0.00f, 0.34f},
            };
    }
    return {};
}

} // namespace modeller
