// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "model_transform_dialog.hpp"
#include "help_topics.hpp"
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

namespace {
wxSpinCtrlDouble *
AddTranslationInput(wxWindow *parent, wxFlexGridSizer *grid, const wxString &label) {
    grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
    auto *input = new wxSpinCtrlDouble(parent,
                                       wxID_ANY,
                                       wxEmptyString,
                                       wxDefaultPosition,
                                       wxDefaultSize,
                                       wxSP_ARROW_KEYS,
                                       -1000000000.0,
                                       1000000000.0,
                                       0.0,
                                       0.1);
    input->SetDigits(4);
    grid->Add(input, 1, wxEXPAND);
    return input;
}
} // namespace

TransformDialog::TransformDialog(wxWindow *parent)
    : wxDialog(parent, wxID_ANY, "Transform selected models") {
    clip_slicer::help::Assign(this, clip_slicer::help::transformDialog);
    auto *root = new wxBoxSizer(wxVERTICAL);

    auto *rotation = new wxStaticBoxSizer(wxVERTICAL, this, "Rotation");
    wxWindow *rotationBox = rotation->GetStaticBox();
    auto *rotationGrid = new wxFlexGridSizer(2, 8, 10);
    rotationGrid->Add(
        new wxStaticText(rotationBox, wxID_ANY, "Angle (degrees):"), 0, wxALIGN_CENTER_VERTICAL);
    angle_ = new wxSpinCtrlDouble(rotationBox,
                                  wxID_ANY,
                                  wxEmptyString,
                                  wxDefaultPosition,
                                  wxDefaultSize,
                                  wxSP_ARROW_KEYS,
                                  0.0,
                                  360.0,
                                  0.0,
                                  1.0);
    angle_->SetDigits(3);
    rotationGrid->Add(angle_, 1, wxEXPAND);
    rotationGrid->AddGrowableCol(1);
    rotation->Add(rotationGrid, 0, wxEXPAND | wxALL, 8);

    axis_ = new wxRadioBox(rotationBox,
                           wxID_ANY,
                           "Axis",
                           wxDefaultPosition,
                           wxDefaultSize,
                           {"X", "Y", "Z"},
                           1,
                           wxRA_SPECIFY_ROWS);
    rotation->Add(axis_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    root->Add(rotation, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

    auto *translation = new wxStaticBoxSizer(wxVERTICAL, this, "Translation");
    wxWindow *translationBox = translation->GetStaticBox();
    auto *translationGrid = new wxFlexGridSizer(2, 8, 10);
    x_ = AddTranslationInput(translationBox, translationGrid, "X:");
    y_ = AddTranslationInput(translationBox, translationGrid, "Y:");
    z_ = AddTranslationInput(translationBox, translationGrid, "Z:");
    translationGrid->AddGrowableCol(1);
    translation->Add(translationGrid, 0, wxEXPAND | wxALL, 8);
    root->Add(translation, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

    auto *scaling = new wxStaticBoxSizer(wxVERTICAL, this, "Scaling");
    wxWindow *scalingBox = scaling->GetStaticBox();
    auto *scalingGrid = new wxFlexGridSizer(2, 8, 10);
    scalingGrid->Add(
        new wxStaticText(scalingBox, wxID_ANY, "Uniform scale:"), 0, wxALIGN_CENTER_VERTICAL);
    scale_ = new wxSpinCtrlDouble(scalingBox,
                                  wxID_ANY,
                                  wxEmptyString,
                                  wxDefaultPosition,
                                  wxDefaultSize,
                                  wxSP_ARROW_KEYS,
                                  0.000001,
                                  1000000.0,
                                  1.0,
                                  0.01);
    scale_->SetDigits(6);
    scalingGrid->Add(scale_, 1, wxEXPAND);
    scalingGrid->AddGrowableCol(1);
    scaling->Add(scalingGrid, 0, wxEXPAND | wxALL, 8);
    root->Add(scaling, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

    root->Add(
        CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    SetSizerAndFit(root);
    SetMinSize({360, GetSize().y});
}

double TransformDialog::AngleDegrees() const {
    return angle_->GetValue();
}

stl_slicer::Vec3 TransformDialog::Axis() const {
    switch (axis_->GetSelection()) {
    case 0:
        return {1.0, 0.0, 0.0};
    case 1:
        return {0.0, 1.0, 0.0};
    default:
        return {0.0, 0.0, 1.0};
    }
}

stl_slicer::Vec3 TransformDialog::Translation() const {
    return {x_->GetValue(), y_->GetValue(), z_->GetValue()};
}

double TransformDialog::UniformScale() const {
    return scale_->GetValue();
}
