// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "model_alignment.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void requireNear(double actual, double expected, const char *message) {
    if (std::abs(actual - expected) > 1e-12)
        throw std::runtime_error(message);
}

void testAlignmentTranslations() {
    stl_slicer::Bounds3 model;
    model.include({2, 4, 6});
    model.include({5, 10, 14});
    stl_slicer::Bounds3 selection;
    selection.include({-3, -2, -1});
    selection.include({20, 30, 40});

    auto translation =
        AlignmentTranslation(model, selection, AlignmentAxis::X, AlignmentType::Minimum);
    requireNear(translation.x, -5, "minimum X alignment was incorrect");
    requireNear(translation.y, 0, "minimum X alignment changed Y");
    requireNear(translation.z, 0, "minimum X alignment changed Z");

    translation = AlignmentTranslation(model, selection, AlignmentAxis::Y, AlignmentType::Center);
    requireNear(translation.x, 0, "center Y alignment changed X");
    requireNear(translation.y, 7, "center Y alignment was incorrect");
    requireNear(translation.z, 0, "center Y alignment changed Z");

    translation = AlignmentTranslation(model, selection, AlignmentAxis::Z, AlignmentType::Maximum);
    requireNear(translation.x, 0, "maximum Z alignment changed X");
    requireNear(translation.y, 0, "maximum Z alignment changed Y");
    requireNear(translation.z, 26, "maximum Z alignment was incorrect");
}

void testEdgeModelStaysInPlace() {
    stl_slicer::Bounds3 model;
    model.include({-3, 1, 2});
    model.include({4, 5, 6});
    stl_slicer::Bounds3 selection;
    selection.include({-3, -10, -20});
    selection.include({30, 40, 50});
    const auto translation =
        AlignmentTranslation(model, selection, AlignmentAxis::X, AlignmentType::Minimum);
    requireNear(translation.x, 0, "edge-defining model should not move");
}
} // namespace

int main() {
    try {
        testAlignmentTranslations();
        testEdgeModelStaysInPlace();
        std::cout << "Model alignment tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}
