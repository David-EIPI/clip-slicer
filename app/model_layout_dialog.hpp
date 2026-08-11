// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "model_alignment.hpp"
#include "model_distribution.hpp"
#include "stl_slicer/geometry.hpp"
#include <array>
#include <cstddef>
#include <wx/dialog.h>

class wxChoice;
class wxNotebook;
class wxRadioBox;
class wxSpinCtrl;
class wxSpinCtrlDouble;

enum class ModelLayoutOperation { Align, Distribute, Multiply };

class ModelLayoutDialog final : public wxDialog {
  public:
    ModelLayoutDialog(wxWindow *parent,
                      const stl_slicer::Vec3 &defaultMultiplyStride,
                      const stl_slicer::Vec3 &defaultDistributionStride,
                      std::size_t selectedModelCount,
                      ModelLayoutOperation initialOperation);
    ~ModelLayoutDialog() override;

    ModelLayoutOperation ActiveOperation() const;
    AlignmentAxis SelectedAlignmentAxis() const;
    AlignmentType SelectedAlignmentType() const;
    DistributionParameters Distribution() const;
    std::array<unsigned int, 3> Copies() const;
    stl_slicer::Vec3 Stride() const;

  private:
    void UpdateHelpTopic();

    wxNotebook *notebook_ = nullptr;
    wxRadioBox *alignmentAxis_ = nullptr;
    wxRadioBox *alignmentType_ = nullptr;
    bool alignVisited_ = false;
    bool distributeVisited_ = false;
    bool multiplyVisited_ = false;
    std::array<wxSpinCtrl *, 3> distributionCells_{};
    std::array<wxSpinCtrlDouble *, 3> distributionStrides_{};
    std::array<wxChoice *, 3> distributionModes_{};
    wxChoice *distributionOrder_ = nullptr;
    std::array<wxSpinCtrl *, 3> copies_{};
    std::array<wxSpinCtrlDouble *, 3> strides_{};
};
