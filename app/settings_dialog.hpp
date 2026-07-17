#pragma once

#include "app_settings.hpp"
#include <wx/dialog.h>

class wxSpinCtrlDouble;
class wxSpinCtrl;

class SettingsDialog final : public wxDialog {
  public:
    SettingsDialog(wxWindow *parent, const AppSettings &settings);
    double LayerThickness() const;
    double FirstLayerOffset() const;
    double ContourHealingThreshold() const;
    double SegmentationTolerance() const;
    double CriticalAngleDegrees() const;
    double OverhangCoefficient() const;
    int OptimizationAttempts() const;
    int OptimizationWorkers() const;
    double OptimizationTolerance() const;
    double SupportSpacing() const;
    double SupportTipTopRadius() const;
    double SupportTipBottomRadius() const;
    double SupportTipHeight() const;
    double SupportLatticeCellSize() const;
    double SupportModelIsolation() const;
    double MinimumSupportAngleDegrees() const;
    double SupportBaseHeight() const;
    double SupportBaseRadius() const;
    double SupportPillarBottomRadius() const;
    double SupportPillarTopRadius() const;
    int SupportCircumferencePoints() const;

  private:
    wxSpinCtrlDouble *layerThickness_ = nullptr;
    wxSpinCtrlDouble *firstLayerOffset_ = nullptr;
    wxSpinCtrlDouble *contourHealingThreshold_ = nullptr;
    wxSpinCtrlDouble *segmentationTolerance_ = nullptr;
    wxSpinCtrlDouble *criticalAngleDegrees_ = nullptr;
    wxSpinCtrlDouble *overhangCoefficient_ = nullptr;
    wxSpinCtrl *optimizationAttempts_ = nullptr;
    wxSpinCtrl *optimizationWorkers_ = nullptr;
    wxSpinCtrlDouble *optimizationTolerance_ = nullptr;
    wxSpinCtrlDouble *supportSpacing_ = nullptr;
    wxSpinCtrlDouble *supportTipTopRadius_ = nullptr;
    wxSpinCtrlDouble *supportTipBottomRadius_ = nullptr;
    wxSpinCtrlDouble *supportTipHeight_ = nullptr;
    wxSpinCtrlDouble *supportLatticeCellSize_ = nullptr;
    wxSpinCtrlDouble *supportModelIsolation_ = nullptr;
    wxSpinCtrlDouble *minimumSupportAngleDegrees_ = nullptr;
    wxSpinCtrlDouble *supportBaseHeight_ = nullptr;
    wxSpinCtrlDouble *supportBaseRadius_ = nullptr;
    wxSpinCtrlDouble *supportPillarBottomRadius_ = nullptr;
    wxSpinCtrlDouble *supportPillarTopRadius_ = nullptr;
    wxSpinCtrl *supportCircumferencePoints_ = nullptr;
};
