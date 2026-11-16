#pragma once

#include <glm/glm.hpp>
#include <limits>

struct AABB
{
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};

    void expand(const glm::vec3 &point)
    {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    void expand(const AABB &other)
    {
        min = glm::min(min, other.min);
        max = glm::max(max, other.max);
    }

    glm::vec3 center() const
    {
        return (min + max) * 0.5f;
    }
    glm::vec3 extents() const
    {
        return (max - min) * 0.5f;
    }

    bool intersects(const AABB &other) const
    {
        return (min.x <= other.max.x && max.x >= other.min.x) && (min.y <= other.max.y && max.y >= other.min.y) &&
               (min.z <= other.max.z && max.z >= other.min.z);
    }

    AABB transformed(const glm::mat4 &m) const
    {
        glm::vec3 newMin(m[3]); // 平移分量
        glm::vec3 newMax(m[3]);

        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                float a = m[j][i] * min[j];
                float b = m[j][i] * max[j];
                newMin[i] += glm::min(a, b);
                newMax[i] += glm::max(a, b);
            }
        }
        return {newMin, newMax};
    }
};
