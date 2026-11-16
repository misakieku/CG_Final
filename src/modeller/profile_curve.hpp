#pragma once

#include <glm/glm.hpp>

#include <array>
#include <span>
#include <vector>

namespace modeller
{

/// Single cubic Bézier segment with 4 2D control points (x = radius r, y = height y).
struct CubicBezierSegment
{
    std::array<glm::vec2, 4> p{};

    [[nodiscard]] glm::vec2 eval(float t) const noexcept;
    [[nodiscard]] glm::vec2 evalDerivative(float t) const noexcept;
};

/// Sampled 2D profile point with exact analytical curve tangent (dr/dt, dy/dt).
struct ProfileSample
{
    glm::vec2 position{};
    glm::vec2 tangent{};
};

enum class ProfilePreset
{
    Chalice,
    Vase,
    Pillar,
    Candle
};

/// Converts user-placed 2D control points into C1-continuous piecewise cubic Bézier segments.
[[nodiscard]] std::vector<CubicBezierSegment> buildPiecewiseBezier(std::span<const glm::vec2> points, float smoothness = 0.5f);

/// Evaluates piecewise cubic Bézier segments into an ordered list of samples + analytical tangents.
[[nodiscard]] std::vector<ProfileSample> sampleProfileCurve(std::span<const CubicBezierSegment> segments, int samplesPerSegment);

/// Returns a built-in starter profile (useful for chalice/vase/pillar/candle SOR generation).
[[nodiscard]] std::vector<glm::vec2> makePresetProfile(ProfilePreset preset);

} // namespace modeller
