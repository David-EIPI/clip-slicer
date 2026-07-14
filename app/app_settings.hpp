#pragma once

#include <wx/string.h>

class AppSettings {
  public:
    static constexpr double defaultContourHealingThreshold = 0.01;
    static constexpr double defaultSegmentationTolerance = 0.01;
    static constexpr double defaultCriticalAngleDegrees = 30.0;
    static constexpr double defaultOverhangCoefficient = 1.0;

    bool Load();
    bool Save() const;
    static wxString FilePath();

    double contourHealingThreshold = defaultContourHealingThreshold;
    double segmentationTolerance = defaultSegmentationTolerance;
    double criticalAngleDegrees = defaultCriticalAngleDegrees;
    double overhangCoefficient = defaultOverhangCoefficient;
};
