#pragma once

#include <agz/utility/d3d11.h>

using Vec2 = agz::math::vec2f;
using Vec3 = agz::math::vec3f;
using Vec4 = agz::math::vec4f;

using Vec2i = agz::math::vec2i;

using Mat4   = agz::math::mat4f_c;
using Trans4 = Mat4::right_transform;

namespace D3D = agz::d3d11;

struct Vertex
{
    Vec3 position;
};

struct Face
{
    using Index = uint32_t;

    bool isQuad = false;
    Index indices[4];
};

struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<Face>   faces;
};
