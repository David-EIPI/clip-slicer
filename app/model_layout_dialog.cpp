// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "model_layout_dialog.hpp"
#include "help_topics.hpp"
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

namespace {
struct RememberedLayoutState {
    AlignmentAxis axis = AlignmentAxis::Z;
    AlignmentType type = AlignmentType::Minimum;
    std::array<unsigned int, 3> copies = {1, 1, 1};
    stl_slicer::Vec3 stride;
    bool hasStride = false;
};

RememberedLayoutState &rememberedLayoutState() {
    static RememberedLayoutState state;
    return state;
}
} // namespace

ModelLayoutDialog::ModelLayoutDialog(wxWindow *parent,
                                     const stl_slicer::Vec3 &defaultStride,
                                     ModelLayoutOperation initialOperation)
    : wxDialog(parent, wxID_ANY, "Arrange selected models") {
    auto *root = new wxBoxSizer(wxVERTICAL);
    notebook_ = new wxNotebook(this, wxID_ANY);

    auto *alignPage = new wxPanel(notebook_);
    clip_slicer::help::Assign(alignPage, clip_slicer::help::alignDialog);
    alignmentAxis_ = new wxRadioBox(alignPage,
                                    wxID_ANY,
                                    "Axis",
                                    wxDefaultPosition,
                                    wxDefaultSize,
                                    {"X", "Y", "Z"},
                                    3,
                                    wxRA_SPECIFY_COLS);
    alignmentType_ = new wxRadioBox(alignPage,
                                    wxID_ANY,
                                    "Alignment",
                                    wxDefaultPosition,
                                    wxDefaultSize,
                                    {"Min", "Center", "Max"},
                                    3,
                                    wxRA_SPECIFY_COLS);
    const RememberedLayoutState &remembered = rememberedLayoutState();
    alignmentAxis_->SetSelection(static_cast<int>(remembered.axis));
    alignmentType_->SetSelection(static_cast<int>(remembered.type));
    auto *alignSizer = new wxBoxSizer(wxVERTICAL);
    alignSizer->Add(alignmentAxis_, 0, wxEXPAND | wxALL, 12);
    alignSizer->Add(alignmentType_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    alignPage->SetSizer(alignSizer);
    notebook_->AddPage(alignPage, "Align", false);

    auto *multiplyPage = new wxPanel(notebook_);
    clip_slicer::help::Assign(multiplyPage, clip_slicer::help::multiplyDialog);
    auto *grid = new wxFlexGridSizer(3, 8, 12);
    grid->Add(new wxStaticText(multiplyPage, wxID_ANY, "Axis"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(multiplyPage, wxID_ANY, "Copies"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(multiplyPage, wxID_ANY, "Stride"), 0, wxALIGN_CENTER_VERTICAL);
    const std::array<wxString, 3> labels = {"X", "Y", "Z"};
    const stl_slicer::Vec3 defaultOrRememberedStride =
        remembered.hasStride ? remembered.stride : defaultStride;
    const std::array<double, 3> defaults = {
        defaultOrRememberedStride.x, defaultOrRememberedStride.y, defaultOrRememberedStride.z};
    for (std::size_t axis = 0; axis < labels.size(); ++axis) {
        grid->Add(
            new wxStaticText(multiplyPage, wxID_ANY, labels[axis]), 0, wxALIGN_CENTER_VERTICAL);
        copies_[axis] = new wxSpinCtrl(multiplyPage,
                                       wxID_ANY,
                                       wxEmptyString,
                                       wxDefaultPosition,
                                       wxDefaultSize,
                                       wxSP_ARROW_KEYS,
                                       1,
                                       65535,
                                       static_cast<int>(remembered.copies[axis]));
        grid->Add(copies_[axis], 1, wxEXPAND);
        strides_[axis] = new wxSpinCtrlDouble(multiplyPage,
                                              wxID_ANY,
                                              wxEmptyString,
                                              wxDefaultPosition,
                                              wxDefaultSize,
                                              wxSP_ARROW_KEYS,
                                              -1000000000.0,
                                              1000000000.0,
                                              defaults[axis],
                                              0.1);
        strides_[axis]->SetDigits(4);
        grid->Add(strides_[axis], 1, wxEXPAND);
    }
    grid->AddGrowableCol(1);
    grid->AddGrowableCol(2);
    auto *multiplySizer = new wxBoxSizer(wxVERTICAL);
    multiplySizer->Add(grid, 1, wxEXPAND | wxALL, 12);
    multiplyPage->SetSizer(multiplySizer);
    notebook_->AddPage(multiplyPage, "Multiply", false);

    const bool initiallyAligning = initialOperation == ModelLayoutOperation::Align;
    alignVisited_ = initiallyAligning;
    multiplyVisited_ = !initiallyAligning;
    notebook_->SetSelection(initiallyAligning ? 0 : 1);
    notebook_->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent &event) {
        if (ActiveOperation() == ModelLayoutOperation::Align)
            alignVisited_ = true;
        else
            multiplyVisited_ = true;
        UpdateHelpTopic();
        event.Skip();
    });
    root->Add(notebook_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    root->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL | wxHELP), 0, wxEXPAND | wxALL, 12);
    SetSizerAndFit(root);
    SetMinSize({FromDIP(430), GetSize().y});
    UpdateHelpTopic();
    clip_slicer::help::Enable(this);
}

ModelLayoutDialog::~ModelLayoutDialog() {
    RememberedLayoutState &remembered = rememberedLayoutState();
    if (alignVisited_) {
        remembered.axis = SelectedAlignmentAxis();
        remembered.type = SelectedAlignmentType();
    }
    if (multiplyVisited_) {
        remembered.copies = Copies();
        remembered.stride = Stride();
        remembered.hasStride = true;
    }
}

ModelLayoutOperation ModelLayoutDialog::ActiveOperation() const {
    return notebook_->GetSelection() == 0 ? ModelLayoutOperation::Align
                                          : ModelLayoutOperation::Multiply;
}

AlignmentAxis ModelLayoutDialog::SelectedAlignmentAxis() const {
    if (alignmentAxis_->GetSelection() == 1)
        return AlignmentAxis::Y;
    if (alignmentAxis_->GetSelection() == 2)
        return AlignmentAxis::Z;
    return AlignmentAxis::X;
}

AlignmentType ModelLayoutDialog::SelectedAlignmentType() const {
    if (alignmentType_->GetSelection() == 1)
        return AlignmentType::Center;
    if (alignmentType_->GetSelection() == 2)
        return AlignmentType::Maximum;
    return AlignmentType::Minimum;
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
    clip_slicer::help::Assign(this,
                              ActiveOperation() == ModelLayoutOperation::Align
                                  ? clip_slicer::help::alignDialog
                                  : clip_slicer::help::multiplyDialog);
}
