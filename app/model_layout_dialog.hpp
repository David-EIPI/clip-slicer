// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "model_alignment.hpp"
#include "stl_slicer/geometry.hpp"
#include <array>
#include <wx/dialog.h>

class wxNotebook;
class wxRadioBox;
class wxSpinCtrl;
class wxSpinCtrlDouble;

enum class ModelLayoutOperation { Align, Multiply };

class ModelLayoutDialog final : public wxDialog {
  public:
    ModelLayoutDialog(wxWindow *parent,
                      const stl_slicer::Vec3 &defaultStride,
                      ModelLayoutOperation initialOperation);

    ModelLayoutOperation ActiveOperation() const;
    AlignmentAxis SelectedAlignmentAxis() const;
    AlignmentType SelectedAlignmentType() const;
    std::array<unsigned int, 3> Copies() const;
    stl_slicer::Vec3 Stride() const;

  private:
    void UpdateHelpTopic();

    wxNotebook *notebook_ = nullptr;
    wxRadioBox *alignmentAxis_ = nullptr;
    wxRadioBox *alignmentType_ = nullptr;
    std::array<wxSpinCtrl *, 3> copies_{};
    std::array<wxSpinCtrlDouble *, 3> strides_{};
};
