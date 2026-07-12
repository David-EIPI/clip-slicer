#include "stl_slicer/cli_writer.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace stl_slicer {
namespace {

void u16(std::ostream& out, std::uint16_t value) {
    const char bytes[] = {static_cast<char>(value), static_cast<char>(value >> 8)};
    out.write(bytes, 2);
}

void u32(std::ostream& out, std::uint32_t value) {
    const char bytes[] = {static_cast<char>(value), static_cast<char>(value >> 8),
                          static_cast<char>(value >> 16), static_cast<char>(value >> 24)};
    out.write(bytes, 4);
}

void real32(std::ostream& out, double value) {
    const float narrowed = static_cast<float>(value);
    if (!std::isfinite(narrowed)) throw std::runtime_error("CLI coordinate is outside REAL range");
    std::uint32_t bits;
    std::memcpy(&bits, &narrowed, sizeof(bits));
    u32(out, bits);
}

std::string number(double value) {
    std::ostringstream out;
    out << std::setprecision(9) << value;
    return out.str();
}

} // namespace

CliWriter::CliWriter(CliWriterOptions options) : options_(options) {
    if (!std::isfinite(options_.unitsMillimeters) || options_.unitsMillimeters <= 0.0)
        throw std::invalid_argument("CLI units must be a positive finite value");
}

void CliWriter::write(const SliceData& data, const std::filesystem::path& path) const {
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("Cannot open CLI file: " + path.string());
    write(data, output);
}

void CliWriter::write(const SliceData& data, std::ostream& output) const {
    const double unit = options_.unitsMillimeters;
    if (!std::isfinite(data.thickness) || data.thickness <= 0.0)
        throw std::invalid_argument("Slice thickness must be a positive finite value");
    const double zOffset = data.layers.empty()
        ? 0.0
        : data.layers.front().z - data.thickness * 0.5;
    output << "$$HEADERSTART\n";
    output << (options_.encoding == CliEncoding::Binary ? "$$BINARY\n" : "$$ASCII\n");
    output << "$$UNITS/" << number(unit) << "\n$$VERSION/200\n";
    if (data.sourceBounds.valid()) {
        output << "$$DIMENSION/" << number(data.sourceBounds.min.x / unit) << ','
               << number(data.sourceBounds.min.y / unit) << ',' << number(data.sourceBounds.min.z / unit) << ','
               << number(data.sourceBounds.max.x / unit) << ',' << number(data.sourceBounds.max.y / unit) << ','
               << number(data.sourceBounds.max.z / unit) << "\n";
    }
    output << "$$LAYERS/" << data.layers.size() << "\n";
    output << "$$LABEL/" << options_.modelId << ",part\n$$HEADEREND";

    if (options_.encoding == CliEncoding::Binary) {
        for (const auto& layer : data.layers) {
            u16(output, 127); real32(output, (layer.z - zOffset) / unit);
            for (const auto& path : layer.paths) {
                if (path.points.size() > std::numeric_limits<std::uint32_t>::max())
                    throw std::runtime_error("CLI polyline has too many points");
                u16(output, 130);
                u32(output, options_.modelId);
                u32(output, static_cast<std::uint32_t>(path.type));
                u32(output, static_cast<std::uint32_t>(path.points.size()));
                for (const auto& point : path.points) {
                    real32(output, point.x / unit); real32(output, point.y / unit);
                }
            }
        }
    } else {
        output << "\n$$GEOMETRYSTART\n";
        for (const auto& layer : data.layers) {
            output << "$$LAYER/" << number((layer.z - zOffset) / unit) << "\n";
            for (const auto& path : layer.paths) {
                output << "$$POLYLINE/" << options_.modelId << ',' << static_cast<unsigned>(path.type)
                       << ',' << path.points.size();
                for (const auto& point : path.points)
                    output << ',' << number(point.x / unit) << ',' << number(point.y / unit);
                output << '\n';
            }
        }
        output << "$$GEOMETRYEND\n";
    }
    if (!output) throw std::runtime_error("Failed while writing CLI data");
}

} // namespace stl_slicer
