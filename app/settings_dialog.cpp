#include "settings_dialog.hpp"
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

SettingsDialog::SettingsDialog(wxWindow *parent, const AppSettings &settings)
    : wxDialog(parent, wxID_ANY, "Settings") {
    auto *root = new wxBoxSizer(wxVERTICAL);
    auto *notebook = new wxNotebook(this, wxID_ANY);
    auto *slicing = new wxPanel(notebook);
    auto *slicingSizer = new wxFlexGridSizer(2, 8, 10);
    slicingSizer->Add(
        new wxStaticText(slicing, wxID_ANY, "Layer thickness (mm):"), 0, wxALIGN_CENTER_VERTICAL);
    layerThickness_ = new wxSpinCtrlDouble(slicing,
                                           wxID_ANY,
                                           wxEmptyString,
                                           wxDefaultPosition,
                                           wxDefaultSize,
                                           wxSP_ARROW_KEYS,
                                           0.000001,
                                           1000.0,
                                           settings.layerThickness,
                                           0.01);
    layerThickness_->SetDigits(6);
    slicingSizer->Add(layerThickness_, 1, wxEXPAND);
    slicingSizer->Add(new wxStaticText(slicing, wxID_ANY, "First-layer offset (mm):"),
                      0,
                      wxALIGN_CENTER_VERTICAL);
    firstLayerOffset_ = new wxSpinCtrlDouble(slicing,
                                             wxID_ANY,
                                             wxEmptyString,
                                             wxDefaultPosition,
                                             wxDefaultSize,
                                             wxSP_ARROW_KEYS,
                                             0.000001,
                                             1000.0,
                                             settings.firstLayerOffset,
                                             0.01);
    firstLayerOffset_->SetDigits(6);
    slicingSizer->Add(firstLayerOffset_, 1, wxEXPAND);
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
    analysisSizer->Add(
        new wxStaticText(analysis, wxID_ANY, "Overhang coefficient:"), 0, wxALIGN_CENTER_VERTICAL);
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
    analysisSizer->Add(
        new wxStaticText(analysis, wxID_ANY, "Optimization attempts:"), 0, wxALIGN_CENTER_VERTICAL);
    optimizationAttempts_ = new wxSpinCtrl(analysis,
                                           wxID_ANY,
                                           wxEmptyString,
                                           wxDefaultPosition,
                                           wxDefaultSize,
                                           wxSP_ARROW_KEYS,
                                           1,
                                           100000,
                                           settings.optimizationAttempts);
    analysisSizer->Add(optimizationAttempts_, 1, wxEXPAND);
    analysisSizer->Add(
        new wxStaticText(analysis, wxID_ANY, "Worker threads:"), 0, wxALIGN_CENTER_VERTICAL);
    optimizationWorkers_ = new wxSpinCtrl(analysis,
                                          wxID_ANY,
                                          wxEmptyString,
                                          wxDefaultPosition,
                                          wxDefaultSize,
                                          wxSP_ARROW_KEYS,
                                          1,
                                          256,
                                          settings.optimizationWorkers);
    analysisSizer->Add(optimizationWorkers_, 1, wxEXPAND);
    analysisSizer->Add(
        new wxStaticText(analysis, wxID_ANY, "Convergence tolerance:"), 0, wxALIGN_CENTER_VERTICAL);
    optimizationTolerance_ = new wxSpinCtrlDouble(analysis,
                                                  wxID_ANY,
                                                  wxEmptyString,
                                                  wxDefaultPosition,
                                                  wxDefaultSize,
                                                  wxSP_ARROW_KEYS,
                                                  0.000001,
                                                  1000000.0,
                                                  settings.optimizationTolerance,
                                                  0.1);
    optimizationTolerance_->SetDigits(3);
    analysisSizer->Add(optimizationTolerance_, 1, wxEXPAND);
    analysisSizer->AddGrowableCol(1);
    analysis->SetSizer(analysisSizer);
    notebook->AddPage(analysis, "Analysis");

    auto *supports = new wxPanel(notebook);
    auto *supportsSizer = new wxBoxSizer(wxVERTICAL);
    auto *spacingSizer = new wxFlexGridSizer(2, 8, 10);
    spacingSizer->Add(
        new wxStaticText(supports, wxID_ANY, "Support spacing (mm):"), 0, wxALIGN_CENTER_VERTICAL);
    supportSpacing_ = new wxSpinCtrlDouble(supports,
                                           wxID_ANY,
                                           wxEmptyString,
                                           wxDefaultPosition,
                                           wxDefaultSize,
                                           wxSP_ARROW_KEYS,
                                           0.001,
                                           1000.0,
                                           settings.supportSpacing,
                                           0.1);
    supportSpacing_->SetDigits(3);
    spacingSizer->Add(supportSpacing_, 1, wxEXPAND);
    spacingSizer->Add(new wxStaticText(supports, wxID_ANY, "Circumference points:"),
                      0,
                      wxALIGN_CENTER_VERTICAL);
    supportCircumferencePoints_ = new wxSpinCtrl(supports,
                                                 wxID_ANY,
                                                 wxEmptyString,
                                                 wxDefaultPosition,
                                                 wxDefaultSize,
                                                 wxSP_ARROW_KEYS,
                                                 3,
                                                 1024,
                                                 settings.supportCircumferencePoints);
    spacingSizer->Add(supportCircumferencePoints_, 1, wxEXPAND);
    spacingSizer->AddGrowableCol(1);
    supportsSizer->Add(spacingSizer, 0, wxEXPAND | wxBOTTOM, 10);

    auto *tipBox = new wxStaticBoxSizer(wxVERTICAL, supports, "Contact tip");
    auto *tipSizer = new wxFlexGridSizer(2, 8, 10);
    const auto addTipDimension = [&](const wxString &label,
                                     wxSpinCtrlDouble *&control,
                                     double value) {
        tipSizer->Add(
            new wxStaticText(tipBox->GetStaticBox(), wxID_ANY, label),
            0,
            wxALIGN_CENTER_VERTICAL);
        control = new wxSpinCtrlDouble(tipBox->GetStaticBox(),
                                       wxID_ANY,
                                       wxEmptyString,
                                       wxDefaultPosition,
                                       wxDefaultSize,
                                       wxSP_ARROW_KEYS,
                                       0.001,
                                       1000.0,
                                       value,
                                       0.1);
        control->SetDigits(3);
        tipSizer->Add(control, 1, wxEXPAND);
    };
    addTipDimension("Top radius (mm):", supportTipTopRadius_, settings.supportTipTopRadius);
    addTipDimension("Bottom radius (mm):",
                    supportTipBottomRadius_,
                    settings.supportTipBottomRadius);
    addTipDimension("Height (mm):", supportTipHeight_, settings.supportTipHeight);
    tipSizer->AddGrowableCol(1);
    tipBox->Add(tipSizer, 1, wxEXPAND | wxALL, 8);
    supportsSizer->Add(tipBox, 0, wxEXPAND);
    supports->SetSizer(supportsSizer);
    notebook->AddPage(supports, "Generator");

    root->Add(notebook, 1, wxEXPAND | wxALL, 12);
    root->Add(
        CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    SetSizerAndFit(root);
    SetMinSize({420, 350});
}

double SettingsDialog::LayerThickness() const {
    return layerThickness_->GetValue();
}

double SettingsDialog::FirstLayerOffset() const {
    return firstLayerOffset_->GetValue();
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

int SettingsDialog::OptimizationAttempts() const {
    return optimizationAttempts_->GetValue();
}

int SettingsDialog::OptimizationWorkers() const {
    return optimizationWorkers_->GetValue();
}

double SettingsDialog::OptimizationTolerance() const {
    return optimizationTolerance_->GetValue();
}

double SettingsDialog::SupportSpacing() const {
    return supportSpacing_->GetValue();
}

double SettingsDialog::SupportTipTopRadius() const {
    return supportTipTopRadius_->GetValue();
}

double SettingsDialog::SupportTipBottomRadius() const {
    return supportTipBottomRadius_->GetValue();
}

double SettingsDialog::SupportTipHeight() const {
    return supportTipHeight_->GetValue();
}

int SettingsDialog::SupportCircumferencePoints() const {
    return supportCircumferencePoints_->GetValue();
}
