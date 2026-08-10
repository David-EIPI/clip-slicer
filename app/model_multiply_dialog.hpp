// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "stl_slicer/geometry.hpp"
#include <array>
#include <wx/dialog.h>

class wxSpinCtrl;
class wxSpinCtrlDouble;

class MultiplyDialog final : public wxDialog {
  public:
    MultiplyDialog(wxWindow *parent, const stl_slicer::Vec3 &defaultStride);

    std::array<unsigned int, 3> Copies() const;
    stl_slicer::Vec3 Stride() const;

  private:
    std::array<wxSpinCtrl *, 3> copies_{};
    std::array<wxSpinCtrlDouble *, 3> strides_{};
};
