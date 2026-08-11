// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "model_layout_dialog.hpp"
#include "help_topics.hpp"
#include <algorithm>
#include <array>
#include <wx/choice.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

namespace {
constexpr std::array<std::array<AlignmentAxis, 3>, 6> axisOrders = {{
    {AlignmentAxis::X, AlignmentAxis::Y, AlignmentAxis::Z},
    {AlignmentAxis::X, AlignmentAxis::Z, AlignmentAxis::Y},
    {AlignmentAxis::Y, AlignmentAxis::X, AlignmentAxis::Z},
    {AlignmentAxis::Y, AlignmentAxis::Z, AlignmentAxis::X},
    {AlignmentAxis::Z, AlignmentAxis::X, AlignmentAxis::Y},
    {AlignmentAxis::Z, AlignmentAxis::Y, AlignmentAxis::X},
}};

struct RememberedLayoutState {
    ModelLayoutOperation activeOperation = ModelLayoutOperation::Align;
    AlignmentAxis axis = AlignmentAxis::Z;
    AlignmentType type = AlignmentType::Minimum;
    std::array<unsigned int, 3> distributionCells = {1, 1, 1};
    stl_slicer::Vec3 distributionStride;
    std::array<DistributionStrideMode, 3> distributionModes = {
        DistributionStrideMode::CenterDistance,
        DistributionStrideMode::CenterDistance,
        DistributionStrideMode::CenterDistance};
    std::array<AlignmentAxis, 3> distributionOrder = axisOrders[0];
    bool hasDistribution = false;
    std::array<unsigned int, 3> copies = {1, 1, 1};
    stl_slicer::Vec3 stride;
    bool hasStride = false;
};

RememberedLayoutState &rememberedLayoutState() {
    static RememberedLayoutState state;
    return state;
}

std::array<unsigned int, 3> defaultCells(std::size_t modelCount) {
    std::array<unsigned int, 3> result = {1, 1, 1};
    std::size_t remaining = std::max<std::size_t>(1, modelCount);
    for (std::size_t axis = 0; axis < result.size() && remaining > 1; ++axis) {
        result[axis] = static_cast<unsigned int>(
            std::min<std::size_t>(remaining, 65535));
        remaining = (remaining + result[axis] - 1) / result[axis];
    }
    return result;
}

int orderSelection(const std::array<AlignmentAxis, 3> &order) {
    for (std::size_t index = 0; index < axisOrders.size(); ++index)
        if (axisOrders[index] == order)
            return static_cast<int>(index);
    return 0;
}
} // namespace

ModelLayoutOperation LastModelLayoutOperation() {
    return rememberedLayoutState().activeOperation;
}

ModelLayoutDialog::ModelLayoutDialog(wxWindow *parent,
                                     const stl_slicer::Vec3 &defaultMultiplyStride,
                                     const stl_slicer::Vec3 &defaultDistributionStride,
                                     std::size_t selectedModelCount,
                                     ModelLayoutOperation initialOperation)
    : wxDialog(parent, wxID_ANY, "Arrange selected models") {
    auto *root = new wxBoxSizer(wxVERTICAL);
    notebook_ = new wxNotebook(this, wxID_ANY);
    const RememberedLayoutState &remembered = rememberedLayoutState();
    const std::array<wxString, 3> labels = {"X", "Y", "Z"};

    auto *alignPage = new wxPanel(notebook_);
    clip_slicer::help::Assign(alignPage, clip_slicer::help::alignDialog);
    alignmentAxis_ = new wxRadioBox(alignPage, wxID_ANY, "Axis", wxDefaultPosition,
                                    wxDefaultSize, {"X", "Y", "Z"}, 3, wxRA_SPECIFY_COLS);
    alignmentType_ = new wxRadioBox(alignPage, wxID_ANY, "Alignment", wxDefaultPosition,
                                    wxDefaultSize, {"Min", "Center", "Max"}, 3,
                                    wxRA_SPECIFY_COLS);
    alignmentAxis_->SetSelection(static_cast<int>(remembered.axis));
    alignmentType_->SetSelection(static_cast<int>(remembered.type));
    auto *alignSizer = new wxBoxSizer(wxVERTICAL);
    alignSizer->Add(alignmentAxis_, 0, wxEXPAND | wxALL, 12);
    alignSizer->Add(alignmentType_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    alignPage->SetSizer(alignSizer);
    notebook_->AddPage(alignPage, "Align", false);

    auto *distributePage = new wxPanel(notebook_);
    clip_slicer::help::Assign(distributePage, clip_slicer::help::distributeDialog);
    auto *distributionGrid = new wxFlexGridSizer(4, 8, 12);
    distributionGrid->Add(new wxStaticText(distributePage, wxID_ANY, "Axis"));
    distributionGrid->Add(new wxStaticText(distributePage, wxID_ANY, "Cells"));
    distributionGrid->Add(new wxStaticText(distributePage, wxID_ANY, "Distance"));
    distributionGrid->Add(new wxStaticText(distributePage, wxID_ANY, "Interpret as"));
    const auto cellDefaults =
        remembered.hasDistribution ? remembered.distributionCells : defaultCells(selectedModelCount);
    const stl_slicer::Vec3 distanceDefaults =
        remembered.hasDistribution ? remembered.distributionStride : defaultDistributionStride;
    const std::array<double, 3> distributionDefaults = {
        distanceDefaults.x, distanceDefaults.y, distanceDefaults.z};
    for (std::size_t axis = 0; axis < labels.size(); ++axis) {
        distributionGrid->Add(new wxStaticText(distributePage, wxID_ANY, labels[axis]),
                              0, wxALIGN_CENTER_VERTICAL);
        distributionCells_[axis] = new wxSpinCtrl(
            distributePage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
            wxSP_ARROW_KEYS, 1, 65535, static_cast<int>(cellDefaults[axis]));
        distributionGrid->Add(distributionCells_[axis], 1, wxEXPAND);
        distributionStrides_[axis] = new wxSpinCtrlDouble(
            distributePage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
            wxSP_ARROW_KEYS, 0.0, 1000000000.0, distributionDefaults[axis], 0.1);
        distributionStrides_[axis]->SetDigits(4);
        distributionGrid->Add(distributionStrides_[axis], 1, wxEXPAND);
        distributionModes_[axis] =
            new wxChoice(distributePage, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                         {"Center distance", "Bounding-box gap"});
        const DistributionStrideMode mode = remembered.hasDistribution
                                                ? remembered.distributionModes[axis]
                                                : DistributionStrideMode::CenterDistance;
        distributionModes_[axis]->SetSelection(
            mode == DistributionStrideMode::CenterDistance ? 0 : 1);
        distributionGrid->Add(distributionModes_[axis], 1, wxEXPAND);
    }
    distributionGrid->AddGrowableCol(1);
    distributionGrid->AddGrowableCol(2);
    distributionGrid->AddGrowableCol(3);
    auto *orderLabel =
        new wxStaticText(distributePage, wxID_ANY, "Fill order (fastest axis first)");
    distributionOrder_ = new wxChoice(
        distributePage, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        {"X, then Y, then Z", "X, then Z, then Y", "Y, then X, then Z",
         "Y, then Z, then X", "Z, then X, then Y", "Z, then Y, then X"});
    distributionOrder_->SetSelection(orderSelection(remembered.distributionOrder));
    auto *distributeSizer = new wxBoxSizer(wxVERTICAL);
    distributeSizer->Add(distributionGrid, 0, wxEXPAND | wxALL, 12);
    distributeSizer->Add(orderLabel, 0, wxLEFT | wxRIGHT, 12);
    distributeSizer->Add(distributionOrder_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    distributePage->SetSizer(distributeSizer);
    notebook_->AddPage(distributePage, "Distribute", false);

    auto *multiplyPage = new wxPanel(notebook_);
    clip_slicer::help::Assign(multiplyPage, clip_slicer::help::multiplyDialog);
    auto *grid = new wxFlexGridSizer(3, 8, 12);
    grid->Add(new wxStaticText(multiplyPage, wxID_ANY, "Axis"));
    grid->Add(new wxStaticText(multiplyPage, wxID_ANY, "Copies"));
    grid->Add(new wxStaticText(multiplyPage, wxID_ANY, "Stride"));
    const stl_slicer::Vec3 defaultOrRememberedStride =
        remembered.hasStride ? remembered.stride : defaultMultiplyStride;
    const std::array<double, 3> defaults = {
        defaultOrRememberedStride.x, defaultOrRememberedStride.y, defaultOrRememberedStride.z};
    for (std::size_t axis = 0; axis < labels.size(); ++axis) {
        grid->Add(new wxStaticText(multiplyPage, wxID_ANY, labels[axis]),
                  0, wxALIGN_CENTER_VERTICAL);
        copies_[axis] = new wxSpinCtrl(multiplyPage, wxID_ANY, wxEmptyString,
                                       wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS,
                                       1, 65535, static_cast<int>(remembered.copies[axis]));
        grid->Add(copies_[axis], 1, wxEXPAND);
        strides_[axis] = new wxSpinCtrlDouble(
            multiplyPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
            wxSP_ARROW_KEYS, -1000000000.0, 1000000000.0, defaults[axis], 0.1);
        strides_[axis]->SetDigits(4);
        grid->Add(strides_[axis], 1, wxEXPAND);
    }
    grid->AddGrowableCol(1);
    grid->AddGrowableCol(2);
    auto *multiplySizer = new wxBoxSizer(wxVERTICAL);
    multiplySizer->Add(grid, 1, wxEXPAND | wxALL, 12);
    multiplyPage->SetSizer(multiplySizer);
    notebook_->AddPage(multiplyPage, "Multiply", false);

    const int initialPage = initialOperation == ModelLayoutOperation::Align
                                ? 0
                                : initialOperation == ModelLayoutOperation::Distribute ? 1 : 2;
    alignVisited_ = initialPage == 0;
    distributeVisited_ = initialPage == 1;
    multiplyVisited_ = initialPage == 2;
    notebook_->SetSelection(initialPage);
    notebook_->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent &event) {
        if (ActiveOperation() == ModelLayoutOperation::Align)
            alignVisited_ = true;
        else if (ActiveOperation() == ModelLayoutOperation::Distribute)
            distributeVisited_ = true;
        else
            multiplyVisited_ = true;
        UpdateHelpTopic();
        event.Skip();
    });
    root->Add(notebook_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    root->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL | wxHELP),
              0, wxEXPAND | wxALL, 12);
    SetSizerAndFit(root);
    SetMinSize({FromDIP(620), GetSize().y});
    UpdateHelpTopic();
    clip_slicer::help::Enable(this);
}

ModelLayoutDialog::~ModelLayoutDialog() {
    RememberedLayoutState &remembered = rememberedLayoutState();
    remembered.activeOperation = ActiveOperation();
    if (alignVisited_) {
        remembered.axis = SelectedAlignmentAxis();
        remembered.type = SelectedAlignmentType();
    }
    if (distributeVisited_) {
        const DistributionParameters parameters = Distribution();
        remembered.distributionCells = parameters.cells;
        remembered.distributionStride = parameters.stride;
        remembered.distributionModes = parameters.strideModes;
        remembered.distributionOrder = parameters.axisOrder;
        remembered.hasDistribution = true;
    }
    if (multiplyVisited_) {
        remembered.copies = Copies();
        remembered.stride = Stride();
        remembered.hasStride = true;
    }
}

ModelLayoutOperation ModelLayoutDialog::ActiveOperation() const {
    if (notebook_->GetSelection() == 0) return ModelLayoutOperation::Align;
    if (notebook_->GetSelection() == 1) return ModelLayoutOperation::Distribute;
    return ModelLayoutOperation::Multiply;
}

AlignmentAxis ModelLayoutDialog::SelectedAlignmentAxis() const {
    return static_cast<AlignmentAxis>(alignmentAxis_->GetSelection());
}
AlignmentType ModelLayoutDialog::SelectedAlignmentType() const {
    return static_cast<AlignmentType>(alignmentType_->GetSelection());
}

DistributionParameters ModelLayoutDialog::Distribution() const {
    DistributionParameters parameters;
    parameters.cells = {static_cast<unsigned int>(distributionCells_[0]->GetValue()),
                        static_cast<unsigned int>(distributionCells_[1]->GetValue()),
                        static_cast<unsigned int>(distributionCells_[2]->GetValue())};
    parameters.stride = {distributionStrides_[0]->GetValue(),
                         distributionStrides_[1]->GetValue(),
                         distributionStrides_[2]->GetValue()};
    for (std::size_t axis = 0; axis < 3; ++axis)
        parameters.strideModes[axis] = distributionModes_[axis]->GetSelection() == 0
                                           ? DistributionStrideMode::CenterDistance
                                           : DistributionStrideMode::BoundingBoxGap;
    parameters.axisOrder = axisOrders[static_cast<std::size_t>(
        std::max(0, distributionOrder_->GetSelection()))];
    return parameters;
}

std::array<unsigned int, 3> ModelLayoutDialog::Copies() const {
    return {static_cast<unsigned int>(copies_[0]->GetValue()),
            static_cast<unsigned int>(copies_[1]->GetValue()),
            static_cast<unsigned int>(copies_[2]->GetValue())};
}
stl_slicer::Vec3 ModelLayoutDialog::Stride() const {
    return {strides_[0]->GetValue(), strides_[1]->GetValue(), strides_[2]->GetValue()};
}

void ModelLayoutDialog::UpdateHelpTopic() {
    const char *topic = clip_slicer::help::multiplyDialog;
    if (ActiveOperation() == ModelLayoutOperation::Align)
        topic = clip_slicer::help::alignDialog;
    else if (ActiveOperation() == ModelLayoutOperation::Distribute)
        topic = clip_slicer::help::distributeDialog;
    clip_slicer::help::Assign(this, topic);
}
