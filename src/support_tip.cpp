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

bool cancelled(const std::atomic<bool> *cancel) {
    return cancel && cancel->load(std::memory_order_relaxed);
}

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

bool projectedIntersection(const Triangle &triangle,
                           const Vec3 &point,
                           double tolerance,
                           double &z) {
    const Vec3 &a = triangle.vertices[0];
    const Vec3 ab = subtract(triangle.vertices[1], a);
    const Vec3 ac = subtract(triangle.vertices[2], a);
    const double determinant = ab.x * ac.y - ab.y * ac.x;
    if (std::abs(determinant) <= tolerance * tolerance)
        return false;

    const double px = point.x - a.x;
    const double py = point.y - a.y;
    const double first = (px * ac.y - py * ac.x) / determinant;
    const double second = (ab.x * py - ab.y * px) / determinant;
    const double barycentricTolerance = tolerance / std::max(1.0, std::sqrt(std::abs(determinant)));
    if (first < -barycentricTolerance || second < -barycentricTolerance ||
        first + second > 1.0 + barycentricTolerance)
        return false;

    z = a.z + first * ab.z + second * ac.z;
    return true;
}

void addTriangle(TriangleMesh &mesh, const Vec3 &first, const Vec3 &second, const Vec3 &third) {
    Triangle triangle;
    triangle.vertices = {first, second, third};
    triangle.normal = normalized(cross(subtract(second, first), subtract(third, first)));
    mesh.addTriangle(std::move(triangle));
}

} // namespace

struct SupportTipBuilder::Impl {
    struct SurfaceSample {
        Vec3 point;
        Vec3 normal;
    };

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
        if (!std::isfinite(options.criticalAngleDegrees) || options.criticalAngleDegrees <= 0.0 ||
            options.criticalAngleDegrees >= 90.0)
            throw std::invalid_argument("Support tip critical angle must be between 0 and 90");

        const double pi = std::acos(-1.0);
        slopeAngle = std::atan2(std::abs(options.bottomRadius - options.topRadius), options.height);
        minimumAxisAngle = std::max(0.0, options.criticalAngleDegrees * pi / 180.0 - slopeAngle);
        unitCircle.reserve(options.circumferencePoints);
        for (std::size_t index = 0; index < options.circumferencePoints; ++index) {
            const double angle =
                2.0 * pi * static_cast<double>(index) /
                static_cast<double>(options.circumferencePoints);
            unitCircle.push_back({std::cos(angle), std::sin(angle), 0.0});
        }
        const Bounds3 &bounds = sourceModel->bounds();
        const double span = bounds.valid() ? std::max({bounds.max.x - bounds.min.x,
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
                                 const Triangle &triangle = sourceModel->triangles()[triangleIndex];
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
                               std::size_t &closestTriangle,
                               Vec3 &closestPoint) const {
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
                                  closestTriangle,
                                  closestPoint);
            findSurfaceCandidates(node.right,
                                  contact,
                                  bestZ,
                                  rayTriangle,
                                  bestDistance,
                                  closestTriangle,
                                  closestPoint);
            return;
        }
        for (std::size_t index = node.begin; index < node.end; ++index) {
            const std::size_t triangleIndex = triangleIndices[index];
            double z = 0.0;
            if (projectedIntersection(
                    sourceModel->triangles()[triangleIndex], contact, tolerance, z) &&
                z <= contact.z + tolerance && z > bestZ) {
                bestZ = z;
                rayTriangle = triangleIndex;
            }
            const Vec3 nearest =
                closestPointOnTriangle(contact, sourceModel->triangles()[triangleIndex]);
            const double distance = squaredLength(subtract(contact, nearest));
            if (nearest.z <= contact.z + tolerance && distance < bestDistance) {
                bestDistance = distance;
                closestTriangle = triangleIndex;
                closestPoint = nearest;
            }
        }
    }

    void collectVerticalIntersections(std::size_t nodeIndex,
                                      const Vec3 &point,
                                      std::vector<double> &intersections) const {
        const Node &node = nodes[nodeIndex];
        if (point.x < node.minX - tolerance || point.x > node.maxX + tolerance ||
            point.y < node.minY - tolerance || point.y > node.maxY + tolerance)
            return;
        if (!node.leaf) {
            collectVerticalIntersections(node.left, point, intersections);
            collectVerticalIntersections(node.right, point, intersections);
            return;
        }
        for (std::size_t index = node.begin; index < node.end; ++index) {
            double z = 0.0;
            if (projectedIntersection(
                    sourceModel->triangles()[triangleIndices[index]], point, tolerance, z) &&
                z > point.z + tolerance)
                intersections.push_back(z);
        }
    }

    bool isInside(const Vec3 &point) const {
        if (nodes.empty())
            return false;
        Vec3 rayPoint = point;
        rayPoint.x += tolerance * 3.141592653589793;
        rayPoint.y += tolerance * 1.618033988749895;
        thread_local std::vector<double> intersections;
        intersections.clear();
        if (intersections.capacity() < 16)
            intersections.reserve(16);
        collectVerticalIntersections(0, rayPoint, intersections);
        std::sort(intersections.begin(), intersections.end());
        std::size_t distinctCount = 0;
        double previous = 0.0;
        for (double z : intersections) {
            if (distinctCount == 0 || z - previous > tolerance * 4.0) {
                ++distinctCount;
                previous = z;
            }
        }
        return distinctCount % 2 != 0;
    }

    Vec3 outwardNormal(const Vec3 &surfacePoint, Vec3 normal) const {
        normal = normalized(normal);
        const double probeBase = std::max(
            {tolerance * 16.0, 1e-4, std::min(options.topRadius, options.bottomRadius) * 1e-3});
        for (double multiplier : {1.0, 10.0, 100.0}) {
            const double distance = probeBase * multiplier;
            const bool positiveInside = isInside(add(surfacePoint, multiply(normal, distance)));
            const bool negativeInside = isInside(add(surfacePoint, multiply(normal, -distance)));
            if (positiveInside != negativeInside)
                return positiveInside ? multiply(normal, -1.0) : normal;
        }
        return normal;
    }

    SurfaceSample surfaceSample(const Vec3 &contact) const {
        const std::size_t noTriangle = sourceModel->triangles().size();
        std::size_t rayTriangle = noTriangle;
        std::size_t closestTriangle = noTriangle;
        double bestZ = -std::numeric_limits<double>::infinity();
        double bestDistance = std::numeric_limits<double>::infinity();
        Vec3 closestPoint;
        if (!nodes.empty())
            findSurfaceCandidates(
                0, contact, bestZ, rayTriangle, bestDistance, closestTriangle, closestPoint);

        std::size_t bestTriangle = closestTriangle != noTriangle ? closestTriangle : rayTriangle;

        if (bestTriangle == noTriangle) {
            for (std::size_t index = 0; index < sourceModel->triangles().size(); ++index) {
                const Vec3 nearest =
                    closestPointOnTriangle(contact, sourceModel->triangles()[index]);
                const double distance = squaredLength(subtract(contact, nearest));
                if (nearest.z <= contact.z + tolerance && distance < bestDistance) {
                    bestDistance = distance;
                    bestTriangle = index;
                    closestPoint = nearest;
                }
            }
        }
        if (bestTriangle == noTriangle)
            return {contact, {0.0, 0.0, -1.0}};
        if (bestTriangle == rayTriangle && closestTriangle == noTriangle)
            closestPoint = {contact.x, contact.y, bestZ};
        Vec3 result = triangleNormal(sourceModel->triangles()[bestTriangle]);
        if (squaredLength(result) <= 1e-15)
            result = {0.0, 0.0, -1.0};
        return {closestPoint, outwardNormal(closestPoint, result)};
    }

    bool coneIsOutside(const Vec3 &contact, const Vec3 &axis) const {
        const Vec3 helper = std::abs(axis.z) < 0.9 ? Vec3{0.0, 0.0, 1.0} : Vec3{1.0, 0.0, 0.0};
        const Vec3 firstRadiusDirection = normalized(cross(helper, axis));
        const Vec3 secondRadiusDirection = normalized(cross(axis, firstRadiusDirection));
        for (double fraction : {0.25, 0.5, 0.75, 1.0}) {
            const Vec3 center = add(contact, multiply(axis, options.height * fraction));
            if (isInside(center))
                return false;
            const double radius =
                options.topRadius + (options.bottomRadius - options.topRadius) * fraction;
            for (std::size_t index = 0; index < options.circumferencePoints; ++index) {
                const Vec3 radial = add(multiply(firstRadiusDirection, unitCircle[index].x),
                                        multiply(secondRadiusDirection, unitCircle[index].y));
                if (isInside(add(center, multiply(radial, radius))))
                    return false;
            }
        }
        return true;
    }

    bool tipAxis(const Vec3 &contact, Vec3 &axis) const {
        const SurfaceSample surface = surfaceSample(contact);
        const Vec3 outward = normalized(surface.normal);
        const double horizontalLength = std::hypot(outward.x, outward.y);
        const double buildAngle = std::atan2(-outward.z, horizontalLength);
        axis = outward;
        if (buildAngle < minimumAxisAngle) {
            if (horizontalLength <= 1e-15)
                return false;
            const double horizontalScale = std::cos(minimumAxisAngle) / horizontalLength;
            axis.x = outward.x * horizontalScale;
            axis.y = outward.y * horizontalScale;
            axis.z = -std::sin(minimumAxisAngle);
        }
        axis = normalized(axis);
        if (axis.z > tolerance || dot(axis, outward) <= 1e-9)
            return false;
        return coneIsOutside(contact, axis);
    }

    bool fallbackAxis(Vec3 &axis, const Vec3 &contact) const {
        axis = normalized(axis);
        const double horizontalLength = std::hypot(axis.x, axis.y);
        const double buildAngle = std::atan2(-axis.z, horizontalLength);
        if (squaredLength(axis) <= 1e-15 || axis.z > tolerance ||
            buildAngle + tolerance < minimumAxisAngle)
            return false;
        return coneIsOutside(contact, axis);
    }

    SupportTipResult makeTip(const Vec3 &contact,
                             const Vec3 &axis,
                             const std::atomic<bool> *cancel) const {
        SupportTipResult result;
        const Vec3 bottomCenter = add(contact, multiply(axis, options.height));
        result.pillarAttachment = bottomCenter;
        const Vec3 helper =
            std::abs(axis.z) < 0.9 ? Vec3{0.0, 0.0, 1.0} : Vec3{1.0, 0.0, 0.0};
        const Vec3 firstRadiusDirection = normalized(cross(helper, axis));
        const Vec3 secondRadiusDirection = normalized(cross(axis, firstRadiusDirection));
        const std::size_t pointCount = options.circumferencePoints;

        std::vector<Vec3> topRing;
        std::vector<Vec3> bottomRing;
        topRing.reserve(pointCount);
        bottomRing.reserve(pointCount);
        for (std::size_t index = 0; index < pointCount; ++index) {
            const Vec3 radial = add(multiply(firstRadiusDirection, unitCircle[index].x),
                                    multiply(secondRadiusDirection, unitCircle[index].y));
            topRing.push_back(add(contact, multiply(radial, options.topRadius)));
            bottomRing.push_back(add(bottomCenter, multiply(radial, options.bottomRadius)));
        }

        result.mesh.setHeader("CLIP Slicer support contact tip");
        result.mesh.reserve(pointCount * 4);
        for (std::size_t index = 0; index < pointCount; ++index) {
            if (cancelled(cancel))
                return {};
            const std::size_t next = (index + 1) % pointCount;
            addTriangle(result.mesh, topRing[index], topRing[next], bottomRing[next]);
            addTriangle(result.mesh, topRing[index], bottomRing[next], bottomRing[index]);
            addTriangle(result.mesh, contact, topRing[next], topRing[index]);
            addTriangle(result.mesh, bottomCenter, bottomRing[index], bottomRing[next]);
        }
        return result;
    }

    std::shared_ptr<const TriangleMesh> sourceModel;
    SupportTipOptions options;
    std::vector<std::size_t> triangleIndices;
    std::vector<Node> nodes;
    std::vector<Vec3> unitCircle;
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
    return buildWithAttachment(contactPoint, cancel).mesh;
}

SupportTipResult SupportTipBuilder::buildWithAttachment(const Vec3 &contactPoint,
                                                        const std::atomic<bool> *cancel) const {
    if (cancel && cancel->load(std::memory_order_relaxed))
        return {};

    Vec3 axis;
    if (!impl_->tipAxis(contactPoint, axis))
        return {};
    return impl_->makeTip(contactPoint, axis, cancel);
}

SupportTipResult SupportTipBuilder::buildWithAxis(const Vec3 &contactPoint,
                                                  const Vec3 &requestedAxis,
                                                  const std::atomic<bool> *cancel) const {
    if (cancelled(cancel))
        return {};
    Vec3 axis = requestedAxis;
    if (!impl_->fallbackAxis(axis, contactPoint))
        return {};
    return impl_->makeTip(contactPoint, axis, cancel);
}

} // namespace stl_slicer
