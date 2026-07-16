#pragma once

#include <wx/string.h>

class AppSettings {
  public:
    static constexpr double defaultLayerThickness = 0.1;
    static constexpr double defaultFirstLayerOffset = 0.05;
    static constexpr double defaultContourHealingThreshold = 0.01;
    static constexpr double defaultSegmentationTolerance = 0.01;
    static constexpr double defaultCriticalAngleDegrees = 30.0;
    static constexpr double defaultOverhangCoefficient = 1.0;
    static constexpr int defaultOptimizationAttempts = 10;
    static constexpr int defaultOptimizationWorkers = 4;
    static constexpr double defaultOptimizationTolerance = 0.1;

    bool Load();
    bool Save() const;
    static wxString FilePath();

    double layerThickness = defaultLayerThickness;
    double firstLayerOffset = defaultFirstLayerOffset;
    double contourHealingThreshold = defaultContourHealingThreshold;
    double segmentationTolerance = defaultSegmentationTolerance;
    double criticalAngleDegrees = defaultCriticalAngleDegrees;
    double overhangCoefficient = defaultOverhangCoefficient;
    int optimizationAttempts = defaultOptimizationAttempts;
    int optimizationWorkers = defaultOptimizationWorkers;
    double optimizationTolerance = defaultOptimizationTolerance;
};
