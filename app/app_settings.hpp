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
    static constexpr double defaultSupportSpacing = 2.0;
    static constexpr double defaultSupportTipTopRadius = 0.25;
    static constexpr double defaultSupportTipBottomRadius = 0.75;
    static constexpr double defaultSupportTipHeight = 2.0;
    static constexpr double defaultSupportLatticeCellSize = 0.5;
    static constexpr double defaultMinimumSupportAngleDegrees = 30.0;
    static constexpr double defaultSupportBaseHeight = 0.5;
    static constexpr double defaultSupportBaseRadius = 2.0;
    static constexpr double defaultSupportPillarBottomRadius = 0.75;
    static constexpr double defaultSupportPillarTopRadius = 0.5;
    static constexpr int defaultSupportCircumferencePoints = 12;

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
    double supportSpacing = defaultSupportSpacing;
    double supportTipTopRadius = defaultSupportTipTopRadius;
    double supportTipBottomRadius = defaultSupportTipBottomRadius;
    double supportTipHeight = defaultSupportTipHeight;
    double supportLatticeCellSize = defaultSupportLatticeCellSize;
    double minimumSupportAngleDegrees = defaultMinimumSupportAngleDegrees;
    double supportBaseHeight = defaultSupportBaseHeight;
    double supportBaseRadius = defaultSupportBaseRadius;
    double supportPillarBottomRadius = defaultSupportPillarBottomRadius;
    double supportPillarTopRadius = defaultSupportPillarTopRadius;
    int supportCircumferencePoints = defaultSupportCircumferencePoints;
};
