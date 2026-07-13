#include "stl_slicer/cli_writer.hpp"
#include "stl_slicer/slicer.hpp"
#include "stl_slicer/stl_reader.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void usage(const char *program) {
    std::cerr << "Usage: " << program
              << " [--ascii] [--tolerance value] [--gap-multiplier value]"
                 " input.stl output.cli [layer-thickness]\n";
}

double parsePositive(const char *text, const char *name) {
    std::size_t consumed = 0;
    const double value = std::stod(text, &consumed);
    if (text[consumed] != '\0' || value <= 0.0)
        throw std::invalid_argument(std::string("Invalid ") + name);
    return value;
}

} // namespace

int main(int argc, char **argv) {
    try {
        bool ascii = false;
        double tolerance = 1e-5;
        double gapMultiplier = 5.0;
        int arg = 1;
        while (arg < argc && std::string(argv[arg]).rfind("--", 0) == 0) {
            const std::string option = argv[arg++];
            if (option == "--ascii")
                ascii = true;
            else if (option == "--tolerance" && arg < argc)
                tolerance = parsePositive(argv[arg++], "tolerance");
            else if (option == "--gap-multiplier" && arg < argc)
                gapMultiplier = parsePositive(argv[arg++], "gap multiplier");
            else {
                usage(argv[0]);
                return 2;
            }
        }
        if (argc - arg < 2 || argc - arg > 3) {
            usage(argv[0]);
            return 2;
        }
        const double thickness =
            argc - arg == 3 ? parsePositive(argv[arg + 2], "layer thickness") : 0.1;

        const auto mesh = stl_slicer::BinaryStlReader{}.read(argv[arg]);
        const auto slices = stl_slicer::Slicer{{thickness, tolerance, gapMultiplier}}.slice(mesh);
        stl_slicer::CliWriter{
            {ascii ? stl_slicer::CliEncoding::Ascii : stl_slicer::CliEncoding::Binary, 1.0, 1}}
            .write(slices, argv[arg + 1]);
        std::cout << "Read " << mesh.triangles().size() << " triangles; wrote "
                  << slices.layers.size() << " layers to " << argv[arg + 1] << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "stl-slicer: " << error.what() << '\n';
        return 1;
    }
}
