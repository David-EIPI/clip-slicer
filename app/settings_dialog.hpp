#pragma once

#include "app_settings.hpp"
#include <wx/dialog.h>

class wxSpinCtrlDouble;

class SettingsDialog final : public wxDialog {
  public:
    SettingsDialog(wxWindow *parent, const AppSettings &settings);
    double ContourHealingThreshold() const;
    double SegmentationTolerance() const;
    double CriticalAngleDegrees() const;
    double OverhangCoefficient() const;

  private:
    wxSpinCtrlDouble *contourHealingThreshold_ = nullptr;
    wxSpinCtrlDouble *segmentationTolerance_ = nullptr;
    wxSpinCtrlDouble *criticalAngleDegrees_ = nullptr;
    wxSpinCtrlDouble *overhangCoefficient_ = nullptr;
};
