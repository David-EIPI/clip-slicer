#include "stl_slicer/support_tip.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace stl_slicer {
namespace {

constexpr std::size_t leafTriangleCount = 16;

struct BuildCancelled {};

Vec3 subtract(const Vec3 &first, const Vec3 &second) {
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

Vec3 add(const Vec3 &first, const Vec3 &second) {
    return {first.x + second.x, first.y + second.y, first.z + second.z};
}

Vec3 multiply(const Vec3 &vector, double scale) {
    return {vector.x * scale, vector.y * scale, vector.z * scale};
}

double dot(const Vec3 &first, const Vec3 &second) {
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

Vec3 cross(const Vec3 &first, const Vec3 &second) {
    return {first.y * second.z - first.z * second.y,
            first.z * second.x - first.x * second.z,
            first.x * second.y - first.y * second.x};
}

double squaredLength(const Vec3 &vector) {
    return dot(vector, vector);
}

Vec3 normalized(Vec3 vector) {
    const double length = std::sqrt(squaredLength(vector));
    if (!std::isfinite(length) || length <= 1e-15)
        return {};
    return multiply(vector, 1.0 / length);
}

Vec3 triangleNormal(const Triangle &triangle) {
    const Vec3 stored = normalized(triangle.normal);
    Vec3 result = normalized(cross(subtract(triangle.vertices[1], triangle.vertices[0]),
                                   subtract(triangle.vertices[2], triangle.vertices[0])));
    if (squaredLength(result) <= 1e-15)
        result = stored;
    else if (squaredLength(stored) > 1e-15 && dot(result, stored) < 0.0)
        result = multiply(result, -1.0);
    if (result.z > 0.0)
        result = multiply(result, -1.0);
    return result;
}

Vec3 closestPointOnTriangle(const Vec3 &point, const Triangle &triangle) {
    const Vec3 &a = triangle.vertices[0];
    const Vec3 &b = triangle.vertices[1];
    const Vec3 &c = triangle.vertices[2];
    const Vec3 ab = subtract(b, a);
    const Vec3 ac = subtract(c, a);
    const Vec3 ap = subtract(point, a);
    const double d1 = dot(ab, ap);
    const double d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0)
        return a;

    const Vec3 bp = subtract(point, b);
    const double d3 = dot(ab, bp);
    const double d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3)
        return b;

    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
        return add(a, multiply(ab, d1 / (d1 - d3)));

    const Vec3 cp = subtract(point, c);
    const double d5 = dot(ab, cp);
    const double d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6)
        return c;

    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
        return add(a, multiply(ac, d2 / (d2 - d6)));

    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && d4 - d3 >= 0.0 && d5 - d6 >= 0.0) {
        const Vec3 bc = subtract(c, b);
        return add(b, multiply(bc, (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    }

    const double denominator = 1.0 / (va + vb + vc);
    return add(a, add(multiply(ab, vb * denominator), multiply(ac, vc * denominator)));
}

bool verticalIntersection(const Triangle &triangle,
                          const Vec3 &contact,
                          double tolerance,
                          double &z) {
    const Vec3 &a = triangle.vertices[0];
    const Vec3 ab = subtract(triangle.vertices[1], a);
    const Vec3 ac = subtract(triangle.vertices[2], a);
    const double determinant = ab.x * ac.y - ab.y * ac.x;
    if (std::abs(determinant) <= tolerance * tolerance)
        return false;

    const double px = contact.x - a.x;
    const double py = contact.y - a.y;
    const double first = (px * ac.y - py * ac.x) / determinant;
    const double second = (ab.x * py - ab.y * px) / determinant;
    const double barycentricTolerance = tolerance / std::max(1.0, std::sqrt(std::abs(determinant)));
    if (first < -barycentricTolerance || second < -barycentricTolerance ||
        first + second > 1.0 + barycentricTolerance)
        return false;

    z = a.z + first * ab.z + second * ac.z;
    return z <= contact.z + tolerance;
}

void addTriangle(TriangleMesh &mesh, const Vec3 &first, const Vec3 &second, const Vec3 &third) {
    Triangle triangle;
    triangle.vertices = {first, second, third};
    triangle.normal = normalized(cross(subtract(second, first), subtract(third, first)));
    mesh.addTriangle(std::move(triangle));
}

} // namespace

struct SupportTipBuilder::Impl {
    struct Node {
        double minX = std::numeric_limits<double>::infinity();
        double minY = std::numeric_limits<double>::infinity();
        double maxX = -std::numeric_limits<double>::infinity();
        double maxY = -std::numeric_limits<double>::infinity();
        std::size_t begin = 0;
        std::size_t end = 0;
        std::size_t left = 0;
        std::size_t right = 0;
        bool leaf = true;
    };

    explicit Impl(std::shared_ptr<const TriangleMesh> model,
                  SupportTipOptions tipOptions,
                  const std::atomic<bool> *cancel)
        : sourceModel(std::move(model)), options(tipOptions), buildCancel(cancel) {
        if (!sourceModel)
            throw std::invalid_argument("Support tip generation requires a source model");
        if (!std::isfinite(options.topRadius) || options.topRadius <= 0.0 ||
            !std::isfinite(options.bottomRadius) || options.bottomRadius <= 0.0 ||
            !std::isfinite(options.height) || options.height <= 0.0)
            throw std::invalid_argument("Support tip dimensions must be positive finite values");
        if (options.circumferencePoints < 3 || options.circumferencePoints > 1024)
            throw std::invalid_argument("Support tip circumference point count must be 3 to 1024");
        if (!std::isfinite(options.criticalAngleDegrees) ||
            options.criticalAngleDegrees <= 0.0 || options.criticalAngleDegrees >= 90.0)
            throw std::invalid_argument("Support tip critical angle must be between 0 and 90");

        const double pi = std::acos(-1.0);
        slopeAngle = std::atan2(std::abs(options.bottomRadius - options.topRadius),
                                options.height);
        minimumAxisAngle = std::max(0.0, options.criticalAngleDegrees * pi / 180.0 - slopeAngle);
        const Bounds3 &bounds = sourceModel->bounds();
        const double span = bounds.valid()
                                ? std::max({bounds.max.x - bounds.min.x,
                                            bounds.max.y - bounds.min.y,
                                            bounds.max.z - bounds.min.z})
                                : 1.0;
        tolerance = std::max(1e-7, span * 1e-10);

        triangleIndices.resize(sourceModel->triangles().size());
        std::iota(triangleIndices.begin(), triangleIndices.end(), std::size_t{0});
        nodes.reserve(triangleIndices.empty() ? 0 : triangleIndices.size() * 2);
        if (!triangleIndices.empty()) {
            try {
                buildNode(0, triangleIndices.size());
            } catch (const BuildCancelled &) {
                nodes.clear();
            }
        }
    }

    std::size_t buildNode(std::size_t begin, std::size_t end) {
        if (buildCancel && buildCancel->load(std::memory_order_relaxed))
            throw BuildCancelled{};
        const std::size_t nodeIndex = nodes.size();
        nodes.push_back({});
        Node bounds;
        bounds.begin = begin;
        bounds.end = end;
        for (std::size_t index = begin; index < end; ++index) {
            if ((index - begin) % 1024 == 0 && buildCancel &&
                buildCancel->load(std::memory_order_relaxed))
                throw BuildCancelled{};
            const Triangle &triangle = sourceModel->triangles()[triangleIndices[index]];
            for (const Vec3 &vertex : triangle.vertices) {
                bounds.minX = std::min(bounds.minX, vertex.x);
                bounds.minY = std::min(bounds.minY, vertex.y);
                bounds.maxX = std::max(bounds.maxX, vertex.x);
                bounds.maxY = std::max(bounds.maxY, vertex.y);
            }
        }
        nodes[nodeIndex] = bounds;
        if (end - begin <= leafTriangleCount)
            return nodeIndex;

        const bool splitX = bounds.maxX - bounds.minX >= bounds.maxY - bounds.minY;
        const std::size_t middle = begin + (end - begin) / 2;
        std::nth_element(triangleIndices.begin() + static_cast<std::ptrdiff_t>(begin),
                         triangleIndices.begin() + static_cast<std::ptrdiff_t>(middle),
                         triangleIndices.begin() + static_cast<std::ptrdiff_t>(end),
                         [&](std::size_t first, std::size_t second) {
                             const auto center = [&](std::size_t triangleIndex) {
                                 const Triangle &triangle =
                                     sourceModel->triangles()[triangleIndex];
                                 const double firstCoordinate =
                                     splitX ? triangle.vertices[0].x : triangle.vertices[0].y;
                                 const double secondCoordinate =
                                     splitX ? triangle.vertices[1].x : triangle.vertices[1].y;
                                 const double thirdCoordinate =
                                     splitX ? triangle.vertices[2].x : triangle.vertices[2].y;
                                 return firstCoordinate + secondCoordinate + thirdCoordinate;
                             };
                             return center(first) < center(second);
                         });
        const std::size_t left = buildNode(begin, middle);
        const std::size_t right = buildNode(middle, end);
        nodes[nodeIndex].leaf = false;
        nodes[nodeIndex].left = left;
        nodes[nodeIndex].right = right;
        return nodeIndex;
    }

    void findSurfaceCandidates(std::size_t nodeIndex,
                               const Vec3 &contact,
                               double &bestZ,
                               std::size_t &rayTriangle,
                               double &bestDistance,
                               std::size_t &closestTriangle) const {
        const Node &node = nodes[nodeIndex];
        if (contact.x < node.minX - tolerance || contact.x > node.maxX + tolerance ||
            contact.y < node.minY - tolerance || contact.y > node.maxY + tolerance)
            return;
        if (!node.leaf) {
            findSurfaceCandidates(node.left,
                                  contact,
                                  bestZ,
                                  rayTriangle,
                                  bestDistance,
                                  closestTriangle);
            findSurfaceCandidates(node.right,
                                  contact,
                                  bestZ,
                                  rayTriangle,
                                  bestDistance,
                                  closestTriangle);
            return;
        }
        for (std::size_t index = node.begin; index < node.end; ++index) {
            const std::size_t triangleIndex = triangleIndices[index];
            double z = 0.0;
            if (verticalIntersection(
                    sourceModel->triangles()[triangleIndex], contact, tolerance, z) &&
                z > bestZ) {
                bestZ = z;
                rayTriangle = triangleIndex;
            }
            const Vec3 nearest =
                closestPointOnTriangle(contact, sourceModel->triangles()[triangleIndex]);
            const double distance = squaredLength(subtract(contact, nearest));
            if (nearest.z <= contact.z + tolerance && distance < bestDistance) {
                bestDistance = distance;
                closestTriangle = triangleIndex;
            }
        }
    }

    Vec3 surfaceNormal(const Vec3 &contact) const {
        const std::size_t noTriangle = sourceModel->triangles().size();
        std::size_t rayTriangle = noTriangle;
        std::size_t closestTriangle = noTriangle;
        double bestZ = -std::numeric_limits<double>::infinity();
        double bestDistance = std::numeric_limits<double>::infinity();
        if (!nodes.empty())
            findSurfaceCandidates(
                0, contact, bestZ, rayTriangle, bestDistance, closestTriangle);

        std::size_t bestTriangle = closestTriangle != noTriangle ? closestTriangle : rayTriangle;

        if (bestTriangle == noTriangle) {
            for (std::size_t index = 0; index < sourceModel->triangles().size(); ++index) {
                const Vec3 nearest =
                    closestPointOnTriangle(contact, sourceModel->triangles()[index]);
                const double distance = squaredLength(subtract(contact, nearest));
                if (nearest.z <= contact.z + tolerance && distance < bestDistance) {
                    bestDistance = distance;
                    bestTriangle = index;
                }
            }
        }
        if (bestTriangle == noTriangle)
            return {0.0, 0.0, -1.0};
        Vec3 result = triangleNormal(sourceModel->triangles()[bestTriangle]);
        if (squaredLength(result) <= 1e-15)
            result = {0.0, 0.0, -1.0};
        return result;
    }

    Vec3 tipAxis(const Vec3 &contact) const {
        Vec3 axis = surfaceNormal(contact);
        const double horizontalLength = std::hypot(axis.x, axis.y);
        const double buildAngle = std::atan2(std::max(0.0, -axis.z), horizontalLength);
        if (buildAngle < minimumAxisAngle && horizontalLength > 1e-15) {
            const double horizontalScale = std::cos(minimumAxisAngle) / horizontalLength;
            axis.x *= horizontalScale;
            axis.y *= horizontalScale;
            axis.z = -std::sin(minimumAxisAngle);
        }
        return normalized(axis);
    }

    std::shared_ptr<const TriangleMesh> sourceModel;
    SupportTipOptions options;
    std::vector<std::size_t> triangleIndices;
    std::vector<Node> nodes;
    double slopeAngle = 0.0;
    double minimumAxisAngle = 0.0;
    double tolerance = 1e-7;
    const std::atomic<bool> *buildCancel = nullptr;
};

SupportTipBuilder::SupportTipBuilder(std::shared_ptr<const TriangleMesh> sourceModel,
                                     SupportTipOptions options,
                                     const std::atomic<bool> *cancel)
    : impl_(std::make_shared<Impl>(std::move(sourceModel), options, cancel)) {}

TriangleMesh SupportTipBuilder::build(const Vec3 &contactPoint,
                                      const std::atomic<bool> *cancel) const {
    TriangleMesh result;
    if (cancel && cancel->load(std::memory_order_relaxed))
        return result;

    const Vec3 axis = impl_->tipAxis(contactPoint);
    const Vec3 bottomCenter = add(contactPoint, multiply(axis, impl_->options.height));
    const Vec3 helper = std::abs(axis.z) < 0.9 ? Vec3{0.0, 0.0, 1.0}
                                                : Vec3{1.0, 0.0, 0.0};
    const Vec3 firstRadiusDirection = normalized(cross(helper, axis));
    const Vec3 secondRadiusDirection = normalized(cross(axis, firstRadiusDirection));
    const std::size_t pointCount = impl_->options.circumferencePoints;
    const double pi = std::acos(-1.0);

    std::vector<Vec3> topRing;
    std::vector<Vec3> bottomRing;
    topRing.reserve(pointCount);
    bottomRing.reserve(pointCount);
    for (std::size_t index = 0; index < pointCount; ++index) {
        const double angle = 2.0 * pi * static_cast<double>(index) /
                             static_cast<double>(pointCount);
        const Vec3 radial = add(multiply(firstRadiusDirection, std::cos(angle)),
                                multiply(secondRadiusDirection, std::sin(angle)));
        topRing.push_back(add(contactPoint, multiply(radial, impl_->options.topRadius)));
        bottomRing.push_back(add(bottomCenter, multiply(radial, impl_->options.bottomRadius)));
    }

    result.setHeader("CLIP Slicer support contact tip");
    result.reserve(pointCount * 4);
    for (std::size_t index = 0; index < pointCount; ++index) {
        if (cancel && cancel->load(std::memory_order_relaxed))
            return {};
        const std::size_t next = (index + 1) % pointCount;
        addTriangle(result, topRing[index], topRing[next], bottomRing[next]);
        addTriangle(result, topRing[index], bottomRing[next], bottomRing[index]);
        addTriangle(result, contactPoint, topRing[next], topRing[index]);
        addTriangle(result, bottomCenter, bottomRing[index], bottomRing[next]);
    }
    return result;
}

} // namespace stl_slicer
