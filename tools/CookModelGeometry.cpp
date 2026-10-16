// Geometry passes of the model cook, driven by tools/cook_models.py (which reads and writes the GLBs).
//
// Request (little-endian): "OYGQ", uint32 version (1), uint32 kind, uint32 group count, then per kind:
//   kind 1, hidden back faces: uint32 ray count, float max ray angle (degrees), float epsilon scale, then per group
//     (one mesh): uint32 primitive count, and per primitive uint32 flags (1 = blocks rays, 2 = classify), uint32 vertex
//     count, uint32 triangle count, float positions[3 * vertices], uint32 indices[3 * triangles].
//     Response: "OYGR", then per group and classified primitive one byte per triangle: 1 when its back is hidden.
//   kind 2, vertex cache order: per group uint32 vertex count, uint32 index count, uint32 indices[index count].
//     Response: "OYGR", then per group the reordered indices (meshopt_optimizeVertexCacheFifo, cache 16, as the
//     runtime loader does for uncooked models).
//
// A triangle's back is hidden when every ray cast from just behind it (epsilon scale times the mesh's bounding
// diagonal) over its back hemisphere (a Fibonacci spiral of directions up to the max angle from the reversed normal)
// hits a triangle that blocks rays. Any hit counts, from either side. Degenerate triangles count as hidden.
#include "tools/CookModelGeometry.h"

#include <meshoptimizer/src/meshoptimizer.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numbers>
#include <stdexcept>
#include <thread>
#include <vector>

namespace OpenYAMM::Tools
{
namespace
{
using Vector = std::array<double, 3>;

Vector subtract(const Vector &left, const Vector &right)
{
    return {left[0] - right[0], left[1] - right[1], left[2] - right[2]};
}

Vector cross(const Vector &left, const Vector &right)
{
    return {left[1] * right[2] - left[2] * right[1], left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0]};
}

double dot(const Vector &left, const Vector &right)
{
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

double length(const Vector &value)
{
    return std::sqrt(dot(value, value));
}

class Reader
{
public:
    explicit Reader(std::vector<uint8_t> bytes)
        : m_bytes(std::move(bytes))
    {
    }

    template <typename T>
    T value()
    {
        T result;
        take(&result, sizeof(T));
        return result;
    }

    template <typename T>
    std::vector<T> values(size_t count)
    {
        std::vector<T> result(count);
        take(result.data(), count * sizeof(T));
        return result;
    }

    void expectEnd() const
    {
        if (m_offset != m_bytes.size())
        {
            throw std::runtime_error("geometry request has trailing bytes");
        }
    }

private:
    void take(void *pTarget, size_t size)
    {
        if (size > m_bytes.size() - m_offset)
        {
            throw std::runtime_error("geometry request is truncated");
        }
        std::memcpy(pTarget, m_bytes.data() + m_offset, size);
        m_offset += size;
    }

    std::vector<uint8_t> m_bytes;
    size_t m_offset = 0;
};

struct Triangle
{
    Vector a;
    Vector edge1;
    Vector edge2;
};

// A bounding volume hierarchy over the ray-blocking triangles, answering "does this ray hit anything".
class Occluders
{
public:
    explicit Occluders(std::vector<Triangle> triangles)
        : m_triangles(std::move(triangles))
    {
        m_order.resize(m_triangles.size());
        for (uint32_t index = 0; index < m_order.size(); ++index)
        {
            m_order[index] = index;
        }
        if (!m_triangles.empty())
        {
            m_nodes.resize(1);
            build(0, 0, uint32_t(m_order.size()));
        }
    }

    bool anyHit(const Vector &origin, const Vector &direction) const
    {
        if (m_nodes.empty())
        {
            return false;
        }
        const Vector inverse = {1.0 / direction[0], 1.0 / direction[1], 1.0 / direction[2]};
        std::array<uint32_t, 128> stack;
        size_t depth = 0;
        stack[depth++] = 0;
        while (depth > 0)
        {
            const Node &node = m_nodes[stack[--depth]];
            if (!boxHit(node, origin, inverse))
            {
                continue;
            }
            if (node.count > 0)
            {
                for (uint32_t slot = node.first; slot < node.first + node.count; ++slot)
                {
                    if (triangleHit(m_triangles[m_order[slot]], origin, direction))
                    {
                        return true;
                    }
                }
                continue;
            }
            stack[depth++] = node.left;
            stack[depth++] = node.left + 1;
        }
        return false;
    }

private:
    struct Node
    {
        Vector low;
        Vector high;
        uint32_t left = 0;     // first child (the second follows it) when count is 0
        uint32_t first = 0;
        uint32_t count = 0;
    };

    void bounds(const Triangle &triangle, Vector &low, Vector &high) const
    {
        const Vector &a = triangle.a;
        const std::array<Vector, 3> corners = {a,
            Vector{a[0] + triangle.edge1[0], a[1] + triangle.edge1[1], a[2] + triangle.edge1[2]},
            Vector{a[0] + triangle.edge2[0], a[1] + triangle.edge2[1], a[2] + triangle.edge2[2]}};
        for (const Vector &corner : corners)
        {
            for (size_t axis = 0; axis < 3; ++axis)
            {
                low[axis] = std::min(low[axis], corner[axis]);
                high[axis] = std::max(high[axis], corner[axis]);
            }
        }
    }

    // Median split on the longest axis; an internal node's children are an adjacent pair.
    void build(uint32_t nodeIndex, uint32_t first, uint32_t count)
    {
        Node node;
        node.low = {INFINITY, INFINITY, INFINITY};
        node.high = {-INFINITY, -INFINITY, -INFINITY};
        for (uint32_t slot = first; slot < first + count; ++slot)
        {
            bounds(m_triangles[m_order[slot]], node.low, node.high);
        }
        if (count <= 4)
        {
            node.first = first;
            node.count = count;
            m_nodes[nodeIndex] = node;
            return;
        }
        const Vector extent = subtract(node.high, node.low);
        const size_t axis = extent[0] >= extent[1] && extent[0] >= extent[2] ? 0 : extent[1] >= extent[2] ? 1 : 2;
        const auto centre = [&](uint32_t index)
        {
            const Triangle &triangle = m_triangles[index];
            return triangle.a[axis] + (triangle.edge1[axis] + triangle.edge2[axis]) / 3.0;
        };
        const uint32_t half = count / 2;
        std::nth_element(m_order.begin() + first, m_order.begin() + first + half, m_order.begin() + first + count,
            [&](uint32_t left, uint32_t right) { return centre(left) < centre(right); });
        node.left = uint32_t(m_nodes.size());
        m_nodes[nodeIndex] = node;
        m_nodes.resize(m_nodes.size() + 2);
        build(node.left, first, half);
        build(node.left + 1, first + half, count - half);
    }

    static bool boxHit(const Node &node, const Vector &origin, const Vector &inverse)
    {
        double near = 0.0;
        double far = INFINITY;
        for (size_t axis = 0; axis < 3; ++axis)
        {
            double t0 = (node.low[axis] - origin[axis]) * inverse[axis];
            double t1 = (node.high[axis] - origin[axis]) * inverse[axis];
            if (t0 > t1)
            {
                std::swap(t0, t1);
            }
            near = std::max(near, std::isnan(t0) ? near : t0);
            far = std::min(far, std::isnan(t1) ? far : t1);
            if (near > far)
            {
                return false;
            }
        }
        return true;
    }

    // Moller-Trumbore, both sides, any distance ahead of the origin.
    static bool triangleHit(const Triangle &triangle, const Vector &origin, const Vector &direction)
    {
        const Vector p = cross(direction, triangle.edge2);
        const double determinant = dot(triangle.edge1, p);
        if (std::abs(determinant) < 1e-18)
        {
            return false;
        }
        const double inverse = 1.0 / determinant;
        const Vector offset = subtract(origin, triangle.a);
        const double u = dot(offset, p) * inverse;
        if (u < 0.0 || u > 1.0)
        {
            return false;
        }
        const Vector q = cross(offset, triangle.edge1);
        const double v = dot(direction, q) * inverse;
        if (v < 0.0 || u + v > 1.0)
        {
            return false;
        }
        return dot(triangle.edge2, q) * inverse > 0.0;
    }

    std::vector<Triangle> m_triangles;
    std::vector<uint32_t> m_order;
    std::vector<Node> m_nodes;
};

struct Primitive
{
    uint32_t flags = 0;
    std::vector<float> positions;
    std::vector<uint32_t> indices;
};

std::vector<Vector> hemisphereDirections(uint32_t count, double maxAngleDegrees)
{
    std::vector<Vector> directions;
    const double lowest = std::cos(maxAngleDegrees * std::numbers::pi / 180.0);
    const double golden = std::numbers::pi * (3.0 - std::sqrt(5.0));
    for (uint32_t index = 0; index < count; ++index)
    {
        const double z = 1.0 - (index + 0.5) / count * (1.0 - lowest);
        const double radius = std::sqrt(std::max(0.0, 1.0 - z * z));
        const double angle = index * golden;
        directions.push_back({radius * std::cos(angle), radius * std::sin(angle), z});
    }
    return directions;
}

Vector position(const Primitive &primitive, uint32_t vertex)
{
    if (size_t(vertex) * 3 + 2 >= primitive.positions.size())
    {
        throw std::runtime_error("geometry request index is out of range");
    }
    return {primitive.positions[vertex * 3], primitive.positions[vertex * 3 + 1], primitive.positions[vertex * 3 + 2]};
}

std::vector<uint8_t> classifyMesh(const std::vector<Primitive> &primitives, const std::vector<Vector> &directions,
    double epsilonScale)
{
    std::vector<Triangle> blockers;
    Vector low = {INFINITY, INFINITY, INFINITY};
    Vector high = {-INFINITY, -INFINITY, -INFINITY};
    for (const Primitive &primitive : primitives)
    {
        if ((primitive.flags & 1) == 0)
        {
            continue;
        }
        for (size_t corner = 0; corner + 2 < primitive.indices.size(); corner += 3)
        {
            const Vector a = position(primitive, primitive.indices[corner]);
            blockers.push_back({a, subtract(position(primitive, primitive.indices[corner + 1]), a),
                subtract(position(primitive, primitive.indices[corner + 2]), a)});
        }
        for (size_t vertex = 0; vertex * 3 < primitive.positions.size(); ++vertex)
        {
            for (size_t axis = 0; axis < 3; ++axis)
            {
                low[axis] = std::min(low[axis], double(primitive.positions[vertex * 3 + axis]));
                high[axis] = std::max(high[axis], double(primitive.positions[vertex * 3 + axis]));
            }
        }
    }
    const double epsilon = blockers.empty() ? 0.0 : epsilonScale * length(subtract(high, low));
    const Occluders occluders(std::move(blockers));

    // Triangles to classify, in request order.
    std::vector<std::array<Vector, 3>> triangles;
    for (const Primitive &primitive : primitives)
    {
        if ((primitive.flags & 2) == 0)
        {
            continue;
        }
        for (size_t corner = 0; corner + 2 < primitive.indices.size(); corner += 3)
        {
            triangles.push_back({position(primitive, primitive.indices[corner]),
                position(primitive, primitive.indices[corner + 1]),
                position(primitive, primitive.indices[corner + 2])});
        }
    }
    std::vector<uint8_t> hidden(triangles.size(), 0);
    std::atomic<size_t> next = 0;
    std::vector<std::jthread> threads;
    const unsigned workers = std::max(1u, std::thread::hardware_concurrency());
    for (unsigned worker = 0; worker < workers; ++worker)
    {
        threads.emplace_back([&]()
        {
            for (size_t index = next.fetch_add(1); index < triangles.size(); index = next.fetch_add(1))
            {
                const std::array<Vector, 3> &corners = triangles[index];
                const Vector normal = cross(subtract(corners[1], corners[0]), subtract(corners[2], corners[0]));
                const double size = length(normal);
                if (size <= 1e-12)
                {
                    hidden[index] = 1;
                    continue;
                }
                const Vector back = {-normal[0] / size, -normal[1] / size, -normal[2] / size};
                const Vector helper = std::abs(back[0]) < 0.9 ? Vector{1.0, 0.0, 0.0} : Vector{0.0, 1.0, 0.0};
                Vector tangent = cross(back, helper);
                const double tangentLength = length(tangent);
                tangent = {tangent[0] / tangentLength, tangent[1] / tangentLength, tangent[2] / tangentLength};
                const Vector bitangent = cross(back, tangent);
                Vector origin;
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    origin[axis] = (corners[0][axis] + corners[1][axis] + corners[2][axis]) / 3.0
                        + back[axis] * epsilon;
                }
                bool enclosed = true;
                for (const Vector &local : directions)
                {
                    const Vector direction = {tangent[0] * local[0] + bitangent[0] * local[1] + back[0] * local[2],
                        tangent[1] * local[0] + bitangent[1] * local[1] + back[1] * local[2],
                        tangent[2] * local[0] + bitangent[2] * local[1] + back[2] * local[2]};
                    if (!occluders.anyHit(origin, direction))
                    {
                        enclosed = false;
                        break;
                    }
                }
                hidden[index] = enclosed ? 1 : 0;
            }
        });
    }
    threads.clear();
    return hidden;
}

std::vector<uint8_t> readFile(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        throw std::runtime_error("cannot read " + path.string());
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), {});
}

void append(std::vector<uint8_t> &target, const void *pData, size_t size)
{
    const uint8_t *pBytes = static_cast<const uint8_t *>(pData);
    target.insert(target.end(), pBytes, pBytes + size);
}
}

int cookModelGeometry(const std::filesystem::path &request, const std::filesystem::path &response)
{
    try
    {
        Reader reader(readFile(request));
        const std::array<char, 4> magic = reader.value<std::array<char, 4>>();
        if (std::memcmp(magic.data(), "OYGQ", 4) != 0 || reader.value<uint32_t>() != 1)
        {
            throw std::runtime_error("not a version 1 geometry request");
        }
        const uint32_t kind = reader.value<uint32_t>();
        const uint32_t groups = reader.value<uint32_t>();
        std::vector<uint8_t> output;
        append(output, "OYGR", 4);
        if (kind == 1)
        {
            const uint32_t rays = reader.value<uint32_t>();
            const float maxAngle = reader.value<float>();
            const float epsilonScale = reader.value<float>();
            const std::vector<Vector> directions = hemisphereDirections(rays, maxAngle);
            for (uint32_t group = 0; group < groups; ++group)
            {
                std::vector<Primitive> primitives(reader.value<uint32_t>());
                for (Primitive &primitive : primitives)
                {
                    primitive.flags = reader.value<uint32_t>();
                    const uint32_t vertices = reader.value<uint32_t>();
                    const uint32_t triangles = reader.value<uint32_t>();
                    primitive.positions = reader.values<float>(size_t(vertices) * 3);
                    primitive.indices = reader.values<uint32_t>(size_t(triangles) * 3);
                }
                const std::vector<uint8_t> hidden = classifyMesh(primitives, directions, epsilonScale);
                append(output, hidden.data(), hidden.size());
            }
        }
        else if (kind == 2)
        {
            for (uint32_t group = 0; group < groups; ++group)
            {
                const uint32_t vertices = reader.value<uint32_t>();
                std::vector<uint32_t> indices = reader.values<uint32_t>(reader.value<uint32_t>());
                if (std::any_of(indices.begin(), indices.end(), [&](uint32_t index) { return index >= vertices; })
                    || indices.size() % 3 != 0)
                {
                    throw std::runtime_error("invalid index buffer in the vertex cache request");
                }
                meshopt_optimizeVertexCacheFifo(indices.data(), indices.data(), indices.size(), vertices, 16);
                append(output, indices.data(), indices.size() * sizeof(uint32_t));
            }
        }
        else
        {
            throw std::runtime_error("unknown geometry request kind " + std::to_string(kind));
        }
        reader.expectEnd();
        std::ofstream stream(response, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char *>(output.data()), std::streamsize(output.size()));
        if (!stream)
        {
            throw std::runtime_error("cannot write " + response.string());
        }
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "Model geometry cook failed: " << exception.what() << '\n';
        return 1;
    }
}
}
