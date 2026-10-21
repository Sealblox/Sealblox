#pragma once

#include <entt/entt.hpp>
#include <bx/math.h>
#include <string>

namespace Components
{
    struct Name
    {
        std::string value;
    };
    struct Transform
    {
        bx::Vec3 position;
        bx::Vec3 rotation;
        bx::Vec3 scale;

        Transform &operator*=(Transform other)
        {
            position = bx::mul(position, other.position);
            rotation = bx::mul(rotation, other.rotation);
            scale = bx::mul(scale, other.scale);
            return *this;
        }
    };
    struct MeshRenderer
    {
        float scalar = 1.0f;
        float fitsize = 0.0f;
    };
}