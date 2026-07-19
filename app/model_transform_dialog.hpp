// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "stl_slicer/geometry.hpp"
#include <wx/dialog.h>

class wxRadioBox;
class wxSpinCtrlDouble;

class TransformDialog final : public wxDialog {
  public:
    explicit TransformDialog(wxWindow *parent);

    double AngleDegrees() const;
    stl_slicer::Vec3 Axis() const;
    stl_slicer::Vec3 Translation() const;
    double UniformScale() const;

  private:
    wxSpinCtrlDouble *angle_ = nullptr;
    wxRadioBox *axis_ = nullptr;
    wxSpinCtrlDouble *x_ = nullptr;
    wxSpinCtrlDouble *y_ = nullptr;
    wxSpinCtrlDouble *z_ = nullptr;
    wxSpinCtrlDouble *scale_ = nullptr;
};
