// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "model_multiply_dialog.hpp"
#include "help_topics.hpp"
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

MultiplyDialog::MultiplyDialog(wxWindow *parent, const stl_slicer::Vec3 &defaultStride)
    : wxDialog(parent, wxID_ANY, "Multiply selected models") {
    clip_slicer::help::Assign(this, clip_slicer::help::multiplyDialog);
    auto *root = new wxBoxSizer(wxVERTICAL);
    auto *notebook = new wxNotebook(this, wxID_ANY);
    auto *page = new wxPanel(notebook);
    clip_slicer::help::Assign(page, clip_slicer::help::multiplyDialog);

    auto *grid = new wxFlexGridSizer(3, 8, 12);
    grid->Add(new wxStaticText(page, wxID_ANY, "Axis"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(page, wxID_ANY, "Copies"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(page, wxID_ANY, "Stride"), 0, wxALIGN_CENTER_VERTICAL);
    const std::array<wxString, 3> labels = {"X", "Y", "Z"};
    const std::array<double, 3> defaults = {defaultStride.x, defaultStride.y, defaultStride.z};
    for (std::size_t axis = 0; axis < labels.size(); ++axis) {
        grid->Add(new wxStaticText(page, wxID_ANY, labels[axis]), 0, wxALIGN_CENTER_VERTICAL);
        copies_[axis] = new wxSpinCtrl(page,
                                       wxID_ANY,
                                       wxEmptyString,
                                       wxDefaultPosition,
                                       wxDefaultSize,
                                       wxSP_ARROW_KEYS,
                                       1,
                                       65535,
                                       1);
        grid->Add(copies_[axis], 1, wxEXPAND);
        strides_[axis] = new wxSpinCtrlDouble(page,
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
    auto *pageSizer = new wxBoxSizer(wxVERTICAL);
    pageSizer->Add(grid, 1, wxEXPAND | wxALL, 12);
    page->SetSizer(pageSizer);
    notebook->AddPage(page, "Multiply", true);
    root->Add(notebook, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    root->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL | wxHELP), 0, wxEXPAND | wxALL, 12);
    SetSizerAndFit(root);
    SetMinSize({FromDIP(430), GetSize().y});
    clip_slicer::help::Enable(this);
}

std::array<unsigned int, 3> MultiplyDialog::Copies() const {
    return {static_cast<unsigned int>(copies_[0]->GetValue()),
            static_cast<unsigned int>(copies_[1]->GetValue()),
            static_cast<unsigned int>(copies_[2]->GetValue())};
}

stl_slicer::Vec3 MultiplyDialog::Stride() const {
    return {strides_[0]->GetValue(), strides_[1]->GetValue(), strides_[2]->GetValue()};
}
