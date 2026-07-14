#include "settings_dialog.hpp"
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

SettingsDialog::SettingsDialog(wxWindow *parent, const AppSettings &settings)
    : wxDialog(parent, wxID_ANY, "Settings") {
    auto *root = new wxBoxSizer(wxVERTICAL);
    auto *notebook = new wxNotebook(this, wxID_ANY);
    auto *slicing = new wxPanel(notebook);
    auto *slicingSizer = new wxFlexGridSizer(2, 8, 10);
    slicingSizer->Add(new wxStaticText(slicing, wxID_ANY, "Contour healing threshold:"),
                      0,
                      wxALIGN_CENTER_VERTICAL);
    contourHealingThreshold_ = new wxSpinCtrlDouble(slicing,
                                                    wxID_ANY,
                                                    wxEmptyString,
                                                    wxDefaultPosition,
                                                    wxDefaultSize,
                                                    wxSP_ARROW_KEYS,
                                                    0.000001,
                                                    10.0,
                                                    settings.contourHealingThreshold,
                                                    0.001);
    contourHealingThreshold_->SetDigits(6);
    slicingSizer->Add(contourHealingThreshold_, 1, wxEXPAND);
    slicingSizer->Add(
        new wxStaticText(slicing, wxID_ANY, "Segmentation tolerance:"), 0, wxALIGN_CENTER_VERTICAL);
    segmentationTolerance_ = new wxSpinCtrlDouble(slicing,
                                                  wxID_ANY,
                                                  wxEmptyString,
                                                  wxDefaultPosition,
                                                  wxDefaultSize,
                                                  wxSP_ARROW_KEYS,
                                                  0.000000001,
                                                  1.0,
                                                  settings.segmentationTolerance,
                                                  0.001);
    segmentationTolerance_->SetDigits(6);
    slicingSizer->Add(segmentationTolerance_, 1, wxEXPAND);
    slicingSizer->AddGrowableCol(1);
    slicing->SetSizer(slicingSizer);
    notebook->AddPage(slicing, "Slicing", true);

    auto *analysis = new wxPanel(notebook);
    auto *analysisSizer = new wxFlexGridSizer(2, 8, 10);
    analysisSizer->Add(new wxStaticText(analysis, wxID_ANY, "Critical angle (degrees):"),
                       0,
                       wxALIGN_CENTER_VERTICAL);
    criticalAngleDegrees_ = new wxSpinCtrlDouble(analysis,
                                                 wxID_ANY,
                                                 wxEmptyString,
                                                 wxDefaultPosition,
                                                 wxDefaultSize,
                                                 wxSP_ARROW_KEYS,
                                                 0.1,
                                                 89.9,
                                                 settings.criticalAngleDegrees,
                                                 1.0);
    criticalAngleDegrees_->SetDigits(1);
    analysisSizer->Add(criticalAngleDegrees_, 1, wxEXPAND);
    analysisSizer->Add(new wxStaticText(analysis, wxID_ANY, "Overhang coefficient:"),
                       0,
                       wxALIGN_CENTER_VERTICAL);
    overhangCoefficient_ = new wxSpinCtrlDouble(analysis,
                                                wxID_ANY,
                                                wxEmptyString,
                                                wxDefaultPosition,
                                                wxDefaultSize,
                                                wxSP_ARROW_KEYS,
                                                0.0,
                                                100.0,
                                                settings.overhangCoefficient,
                                                0.1);
    overhangCoefficient_->SetDigits(2);
    analysisSizer->Add(overhangCoefficient_, 1, wxEXPAND);
    analysisSizer->AddGrowableCol(1);
    analysis->SetSizer(analysisSizer);
    notebook->AddPage(analysis, "Analysis");

    root->Add(notebook, 1, wxEXPAND | wxALL, 12);
    root->Add(
        CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    SetSizerAndFit(root);
    SetMinSize({420, 180});
}

double SettingsDialog::ContourHealingThreshold() const {
    return contourHealingThreshold_->GetValue();
}

double SettingsDialog::SegmentationTolerance() const {
    return segmentationTolerance_->GetValue();
}

double SettingsDialog::CriticalAngleDegrees() const {
    return criticalAngleDegrees_->GetValue();
}

double SettingsDialog::OverhangCoefficient() const {
    return overhangCoefficient_->GetValue();
}
