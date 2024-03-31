#ifndef ENGINE_OBJECT_STRUCTURE_HPP
#define ENGINE_OBJECT_STRUCTURE_HPP

#include "glm/vec3.hpp"

namespace ida {
// bounding box
struct AABB {
    glm::vec3 min;
    glm::vec3 max;

    [[nodiscard]] bool IsContain(const glm::vec3& point) const {
        return (point.x >= min.x && point.x <= max.x) &&
               (point.y >= min.y && point.y <= max.y) &&
               (point.z >= min.z && point.z <= max.z);
    }

    AABB operator+(const glm::vec3& point) const {
        return AABB{min + point, max + point};
    }

    AABB operator-(const glm::vec3& point) const {
        return AABB{min - point, max - point};
    }

    AABB operator*(const glm::vec3& point) const {
        return AABB{min * point, max * point};
    }

    AABB operator/(const glm::vec3& point) const {
        return AABB{min / point, max / point};
    }
};

// bounding sphere
struct BSphere {
    glm::vec3 center;
    float radius;
};

// ray
struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
};

// plane
struct Plane {
    glm::vec3 normal;
    float distance;
};

// frustum
struct Frustum {
    Plane planes[6];
};

// intersection test
bool Intersect(const AABB& a, const AABB& b);
bool Intersect(const AABB& a, const BSphere& b);
bool Intersect(const AABB& a, const Ray& b);
bool Intersect(const AABB& a, const Frustum& b);
bool Intersect(const BSphere& a, const BSphere& b);
bool Intersect(const BSphere& a, const Ray& b);
bool Intersect(const BSphere& a, const Frustum& b);

} // namespace ida

#endif // ENGINE_OBJECT_STRUCTURE_HPP
