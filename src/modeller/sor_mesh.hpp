#pragma once

#include "common/dat_mesh.hpp"
#include "modeller/profile_curve.hpp"

#include <span>

namespace modeller
{

/// Builds a closed-seam Surface of Revolution (SOR) around the Y-axis with analytical differential normals.
[[nodiscard]] cg::DatMeshData buildSorMesh(std::span<const ProfileSample> samples, int radialSegments);

} // namespace modeller
