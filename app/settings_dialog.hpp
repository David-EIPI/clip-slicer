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
};
