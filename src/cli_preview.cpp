// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "stl_slicer/cli_preview.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace stl_slicer {
namespace {

struct PreviewBounds {
    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double minZ = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    double maxZ = -std::numeric_limits<double>::infinity();
};

PreviewBounds previewBounds(const SliceData &data) {
    PreviewBounds bounds;
    for (const SliceLayer &layer : data.layers) {
        if (!std::isfinite(layer.z))
            throw std::invalid_argument("CLI preview has a non-finite layer height");
        bounds.minZ = std::min(bounds.minZ, layer.z - data.thickness * 0.5);
        bounds.maxZ = std::max(bounds.maxZ, layer.z + data.thickness * 0.5);
        for (const SlicePath &path : layer.paths) {
            for (const Vec2 &point : path.points) {
                if (!std::isfinite(point.x) || !std::isfinite(point.y))
                    throw std::invalid_argument("CLI preview has a non-finite path coordinate");
                bounds.minX = std::min(bounds.minX, point.x);
                bounds.maxX = std::max(bounds.maxX, point.x);
                bounds.minY = std::min(bounds.minY, point.y);
                bounds.maxY = std::max(bounds.maxY, point.y);
            }
        }
    }
    if (!(bounds.minX <= bounds.maxX))
        bounds.minX = bounds.maxX = 0.0;
    if (!(bounds.minY <= bounds.maxY))
        bounds.minY = bounds.maxY = 0.0;
    if (!(bounds.minZ <= bounds.maxZ))
        bounds.minZ = bounds.maxZ = 0.0;
    return bounds;
}

CliPreview sizedPreview(double horizontalSpan, double verticalSpan) {
    const double horizontalScale = horizontalSpan > 0.0 ? 64.0 / horizontalSpan
                                                          : std::numeric_limits<double>::infinity();
    const double verticalScale = verticalSpan > 0.0 ? 128.0 / verticalSpan
                                                      : std::numeric_limits<double>::infinity();
    const double scale = std::min(horizontalScale, verticalScale);
    CliPreview preview;
    preview.width = horizontalSpan > 0.0
                        ? static_cast<std::uint16_t>(
                              std::clamp(std::ceil(horizontalSpan * scale), 1.0, 64.0))
                        : 1;
    preview.height = verticalSpan > 0.0
                         ? static_cast<std::uint16_t>(
                               std::clamp(std::ceil(verticalSpan * scale), 1.0, 128.0))
                         : 1;
    preview.pixels.assign(static_cast<std::size_t>(preview.width) * preview.height, 0);
    return preview;
}

std::size_t pixelIndex(double value, double minimum, double maximum, std::size_t count) {
    if (count == 1 || maximum <= minimum)
        return 0;
    const double coordinate = (value - minimum) * static_cast<double>(count) /
                              (maximum - minimum);
    return static_cast<std::size_t>(std::clamp(
        std::floor(coordinate), 0.0, static_cast<double>(count - 1)));
}

void setPixel(CliPreview &preview, std::size_t column, std::size_t row) {
    preview.pixels[row * preview.width + column] = 1;
}

double signedArea(const SlicePath &path) {
    double twiceArea = 0.0;
    for (std::size_t index = 0; index < path.points.size(); ++index) {
        const Vec2 &first = path.points[index];
        const Vec2 &second = path.points[(index + 1) % path.points.size()];
        twiceArea += first.x * second.y - second.x * first.y;
    }
    return twiceArea;
}

struct Crossing {
    double x = 0.0;
    int winding = 0;
};

struct FillPath {
    const SlicePath *path = nullptr;
    int windingFactor = 0;
};

void fillInterval(std::vector<std::uint8_t> &grid,
                  std::size_t width,
                  std::size_t row,
                  double left,
                  double right,
                  const PreviewBounds &bounds) {
    if (right <= left || bounds.maxX <= bounds.minX)
        return;
    const double scale = static_cast<double>(width) / (bounds.maxX - bounds.minX);
    const auto first = static_cast<long long>(std::floor((left - bounds.minX) * scale));
    const auto last = static_cast<long long>(std::ceil((right - bounds.minX) * scale));
    for (long long column = std::max(0LL, first);
         column < std::min(static_cast<long long>(width), last);
         ++column)
        grid[row * width + static_cast<std::size_t>(column)] = 1;
}

void rasterizeLayer(const SliceLayer &layer,
                    const PreviewBounds &bounds,
                    std::size_t width,
                    std::size_t height,
                    std::vector<std::uint8_t> &grid) {
    std::fill(grid.begin(), grid.end(), 0);
    const double xSpan = std::max(bounds.maxX - bounds.minX, 1.0);
    const double ySpan = std::max(bounds.maxY - bounds.minY, 1.0);

    std::vector<FillPath> fillPaths;
    fillPaths.reserve(layer.paths.size());
    for (const SlicePath &path : layer.paths) {
        if (path.points.size() < 3 ||
            (path.type == PathType::Open &&
             squaredDistance(path.points.front(), path.points.back()) > 1e-12))
            continue;
        const double area = signedArea(path);
        if (area == 0.0)
            continue;
        const int orientation = path.type == PathType::Open ? 1 : (area > 0.0 ? 1 : -1);
        const int material = path.type == PathType::Internal ? -1 : 1;
        fillPaths.push_back({&path, orientation * material});
    }

    std::vector<std::vector<Crossing>> rowCrossings(height);
    if (bounds.maxY > bounds.minY) {
        const double rowScale = static_cast<double>(height) / (bounds.maxY - bounds.minY);
        for (const FillPath &fillPath : fillPaths) {
            const SlicePath &path = *fillPath.path;
            for (std::size_t index = 0; index < path.points.size(); ++index) {
                const Vec2 &first = path.points[index];
                const Vec2 &second = path.points[(index + 1) % path.points.size()];
                if (first.y == second.y)
                    continue;
                const double lower = std::min(first.y, second.y);
                const double upper = std::max(first.y, second.y);
                const long long firstRow = static_cast<long long>(
                    std::floor((lower - bounds.minY) * rowScale - 0.5));
                const long long lastRow = static_cast<long long>(
                    std::ceil((upper - bounds.minY) * rowScale - 0.5));
                for (long long row = std::max(0LL, firstRow);
                     row <= std::min(static_cast<long long>(height) - 1, lastRow);
                     ++row) {
                    const double y = bounds.minY +
                                     (static_cast<double>(row) + 0.5) / rowScale;
                    if (y < lower || y >= upper)
                        continue;
                    const double x = first.x + (y - first.y) *
                                                   (second.x - first.x) / (second.y - first.y);
                    rowCrossings[static_cast<std::size_t>(row)].push_back(
                        {x, (second.y > first.y ? -1 : 1) * fillPath.windingFactor});
                }
            }
        }
    }
    for (std::size_t row = 0; row < height; ++row) {
        std::vector<Crossing> &crossings = rowCrossings[row];
        std::sort(crossings.begin(), crossings.end(), [](const Crossing &a, const Crossing &b) {
            return a.x < b.x;
        });
        int winding = 0;
        for (std::size_t index = 0; index < crossings.size(); ++index) {
            winding += crossings[index].winding;
            if (winding != 0 && index + 1 < crossings.size())
                fillInterval(grid,
                             width,
                             row,
                             crossings[index].x,
                             crossings[index + 1].x,
                             bounds);
        }
    }

    // Preserve features thinner than one scanline in a miniature silhouette.
    for (const SlicePath &path : layer.paths) {
        if (path.points.size() < 2)
            continue;
        for (std::size_t index = 0; index < path.points.size(); ++index) {
            if (path.type == PathType::Open && index + 1 == path.points.size())
                break;
            const Vec2 &first = path.points[index];
            const Vec2 &second = path.points[(index + 1) % path.points.size()];
            const double dx = (second.x - first.x) * width / xSpan;
            const double dy = (second.y - first.y) * height / ySpan;
            const std::size_t steps = static_cast<std::size_t>(
                std::ceil(std::max(std::abs(dx), std::abs(dy)) * 2.0));
            for (std::size_t step = 0; step <= steps; ++step) {
                const double fraction = steps == 0 ? 0.0 : static_cast<double>(step) / steps;
                const double x = first.x + (second.x - first.x) * fraction;
                const double y = first.y + (second.y - first.y) * fraction;
                const std::size_t column = pixelIndex(x, bounds.minX, bounds.maxX, width);
                const std::size_t row = pixelIndex(y, bounds.minY, bounds.maxY, height);
                grid[row * width + column] = 1;
            }
        }
    }
}

void appendVarint(std::vector<std::uint8_t> &bytes, std::size_t value) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7fU);
        value >>= 7U;
        if (value != 0)
            byte |= 0x80U;
        bytes.push_back(byte);
    } while (value != 0);
}

} // namespace

std::array<CliPreview, 3> makeCliPreviews(const SliceData &data) {
    if (!std::isfinite(data.thickness) || data.thickness <= 0.0)
        throw std::invalid_argument("Slice thickness must be positive for CLI previews");
    const PreviewBounds bounds = previewBounds(data);
    const double xSpan = bounds.maxX - bounds.minX;
    const double ySpan = bounds.maxY - bounds.minY;
    const double zSpan = bounds.maxZ - bounds.minZ;
    std::array<CliPreview, 3> previews{
        sizedPreview(ySpan, zSpan),
        sizedPreview(xSpan, zSpan),
        sizedPreview(xSpan, ySpan)};

    const std::size_t gridWidth = std::max<std::size_t>(previews[1].width, previews[2].width);
    const std::size_t gridHeight = std::max<std::size_t>(previews[0].width, previews[2].height);
    std::vector<std::uint8_t> grid(gridWidth * gridHeight);
    for (const SliceLayer &layer : data.layers) {
        rasterizeLayer(layer, bounds, gridWidth, gridHeight, grid);
        std::vector<std::uint8_t> occupiedX(gridWidth, 0);
        std::vector<std::uint8_t> occupiedY(gridHeight, 0);
        for (std::size_t row = 0; row < gridHeight; ++row) {
            for (std::size_t column = 0; column < gridWidth; ++column) {
                if (grid[row * gridWidth + column] == 0)
                    continue;
                occupiedX[column] = 1;
                occupiedY[row] = 1;
                const double x = bounds.minX +
                                 (static_cast<double>(column) + 0.5) * xSpan / gridWidth;
                const double y = bounds.minY +
                                 (static_cast<double>(row) + 0.5) * ySpan / gridHeight;
                const std::size_t topColumn = previews[2].width - 1 -
                    pixelIndex(x, bounds.minX, bounds.maxX, previews[2].width);
                const std::size_t topRow = previews[2].height - 1 -
                    pixelIndex(y, bounds.minY, bounds.maxY, previews[2].height);
                setPixel(previews[2], topColumn, topRow);
            }
        }
        const double low = layer.z - data.thickness * 0.5;
        const double high = layer.z + data.thickness * 0.5;
        for (std::size_t previewIndex = 0; previewIndex < 2; ++previewIndex) {
            CliPreview &preview = previews[previewIndex];
            for (std::size_t row = 0; row < preview.height; ++row) {
                const double rowHigh = bounds.maxZ - row * zSpan / preview.height;
                const double rowLow = bounds.maxZ - (row + 1) * zSpan / preview.height;
                if (high <= rowLow || low >= rowHigh)
                    continue;
                const auto &occupied = previewIndex == 0 ? occupiedY : occupiedX;
                for (std::size_t column = 0; column < occupied.size(); ++column) {
                    if (occupied[column] == 0)
                        continue;
                    const double horizontal = previewIndex == 0
                        ? bounds.minY + (static_cast<double>(column) + 0.5) * ySpan / gridHeight
                        : bounds.minX + (static_cast<double>(column) + 0.5) * xSpan / gridWidth;
                    const std::size_t imageColumn = pixelIndex(
                        horizontal,
                        previewIndex == 0 ? bounds.minY : bounds.minX,
                        previewIndex == 0 ? bounds.maxY : bounds.maxX,
                        preview.width);
                    setPixel(preview,
                             previewIndex == 0 ? preview.width - 1 - imageColumn : imageColumn,
                             row);
                }
            }
        }
    }
    return previews;
}

std::string encodeCliPreviewBase64(const CliPreview &preview) {
    if (preview.pixels.size() != static_cast<std::size_t>(preview.width) * preview.height)
        throw std::invalid_argument("CLI preview pixel count does not match dimensions");
    std::vector<std::uint8_t> runs;
    std::uint8_t current = 0;
    std::size_t count = 0;
    for (const std::uint8_t pixel : preview.pixels) {
        if (pixel > 1)
            throw std::invalid_argument("CLI preview pixels must be monochrome");
        if (pixel != current) {
            appendVarint(runs, count);
            current = pixel;
            count = 0;
        }
        ++count;
    }
    appendVarint(runs, count);

    constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((runs.size() + 2) / 3) * 4);
    for (std::size_t index = 0; index < runs.size(); index += 3) {
        const std::uint32_t value =
            (static_cast<std::uint32_t>(runs[index]) << 16U) |
            (index + 1 < runs.size() ? static_cast<std::uint32_t>(runs[index + 1]) << 8U : 0U) |
            (index + 2 < runs.size() ? runs[index + 2] : 0U);
        encoded.push_back(alphabet[(value >> 18U) & 63U]);
        encoded.push_back(alphabet[(value >> 12U) & 63U]);
        encoded.push_back(index + 1 < runs.size() ? alphabet[(value >> 6U) & 63U] : '=');
        encoded.push_back(index + 2 < runs.size() ? alphabet[value & 63U] : '=');
    }
    return encoded;
}

CliPreview decodeCliPreviewBase64(std::uint16_t width,
                                 std::uint16_t height,
                                 std::string_view encoded) {
    if (width == 0 || width > 64 || height == 0 || height > 128 ||
        encoded.empty() || encoded.size() % 4 != 0)
        throw std::invalid_argument("Invalid CLI preview dimensions or base64 length");
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<std::uint8_t> bytes;
    bytes.reserve(encoded.size() / 4 * 3);
    for (std::size_t index = 0; index < encoded.size(); index += 4) {
        std::uint32_t value = 0;
        int padding = 0;
        for (std::size_t position = 0; position < 4; ++position) {
            const char character = encoded[index + position];
            if (character == '=') {
                if (position < 2 || index + 4 != encoded.size())
                    throw std::invalid_argument("Invalid CLI preview base64 padding");
                ++padding;
                value <<= 6U;
                continue;
            }
            if (padding != 0)
                throw std::invalid_argument("Invalid CLI preview base64 padding");
            const std::size_t digit = alphabet.find(character);
            if (digit == std::string_view::npos)
                throw std::invalid_argument("Invalid CLI preview base64 character");
            value = (value << 6U) | static_cast<std::uint32_t>(digit);
        }
        bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
        if (padding < 2)
            bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
        if (padding == 0)
            bytes.push_back(static_cast<std::uint8_t>(value));
    }

    CliPreview preview;
    preview.width = width;
    preview.height = height;
    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    preview.pixels.reserve(pixelCount);
    std::size_t position = 0;
    std::uint8_t pixel = 0;
    while (position < bytes.size()) {
        std::size_t run = 0;
        unsigned shift = 0;
        for (;;) {
            if (position == bytes.size() || shift >= sizeof(std::size_t) * 8)
                throw std::invalid_argument("Invalid CLI preview run length");
            const std::uint8_t byte = bytes[position++];
            if ((byte & 0x7fU) > (pixelCount - preview.pixels.size()) >> shift)
                throw std::invalid_argument("CLI preview run exceeds image size");
            run |= static_cast<std::size_t>(byte & 0x7fU) << shift;
            if ((byte & 0x80U) == 0)
                break;
            shift += 7U;
        }
        preview.pixels.insert(preview.pixels.end(), run, pixel);
        pixel ^= 1U;
    }
    if (preview.pixels.size() != pixelCount)
        throw std::invalid_argument("CLI preview runs do not fill image");
    return preview;
}

} // namespace stl_slicer
