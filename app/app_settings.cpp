#include "app_settings.hpp"
#include <cmath>
#include <wx/fileconf.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

namespace {

wxString settingsDirectory() {
    return wxStandardPaths::Get().GetUserConfigDir() + wxFILE_SEP_PATH + "clip-slicer";
}

} // namespace

wxString AppSettings::FilePath() {
    return wxFileName(settingsDirectory(), "settings.ini").GetFullPath();
}

bool AppSettings::Load() {
    layerThickness = defaultLayerThickness;
    firstLayerOffset = defaultFirstLayerOffset;
    contourHealingThreshold = defaultContourHealingThreshold;
    segmentationTolerance = defaultSegmentationTolerance;
    criticalAngleDegrees = defaultCriticalAngleDegrees;
    overhangCoefficient = defaultOverhangCoefficient;
    optimizationAttempts = defaultOptimizationAttempts;
    optimizationWorkers = defaultOptimizationWorkers;
    optimizationTolerance = defaultOptimizationTolerance;
    supportSpacing = defaultSupportSpacing;
    supportTipTopRadius = defaultSupportTipTopRadius;
    supportTipBottomRadius = defaultSupportTipBottomRadius;
    supportTipHeight = defaultSupportTipHeight;
    supportLatticeCellSize = defaultSupportLatticeCellSize;
    supportModelIsolation = defaultSupportModelIsolation;
    minimumSupportAngleDegrees = defaultMinimumSupportAngleDegrees;
    supportBaseHeight = defaultSupportBaseHeight;
    supportBaseRadius = defaultSupportBaseRadius;
    supportPillarBottomRadius = defaultSupportPillarBottomRadius;
    supportPillarTopRadius = defaultSupportPillarTopRadius;
    supportCircumferencePoints = defaultSupportCircumferencePoints;
    crossSectionDisplayDistance = defaultCrossSectionDisplayDistance;
    if (!wxFileExists(FilePath()))
        return true;

    wxFileConfig config(
        wxEmptyString, wxEmptyString, FilePath(), wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
    double value = defaultLayerThickness;
    config.Read("/slicing/layerThickness", &value, defaultLayerThickness);
    if (std::isfinite(value) && value > 0.0)
        layerThickness = value;
    value = defaultFirstLayerOffset;
    config.Read("/slicing/firstLayerOffset", &value, defaultFirstLayerOffset);
    if (std::isfinite(value) && value > 0.0)
        firstLayerOffset = value;
    value = defaultContourHealingThreshold;
    config.Read("/slicing/contourHealingThreshold", &value, defaultContourHealingThreshold);
    if (std::isfinite(value) && value > 0.0)
        contourHealingThreshold = value;
    value = defaultSegmentationTolerance;
    config.Read("/slicing/segmentationTolerance", &value, defaultSegmentationTolerance);
    if (std::isfinite(value) && value > 0.0)
        segmentationTolerance = value;
    value = defaultCriticalAngleDegrees;
    config.Read("/analysis/criticalAngleDegrees", &value, defaultCriticalAngleDegrees);
    if (std::isfinite(value) && value > 0.0 && value < 90.0)
        criticalAngleDegrees = value;
    value = defaultOverhangCoefficient;
    config.Read("/analysis/overhangCoefficient", &value, defaultOverhangCoefficient);
    if (std::isfinite(value) && value >= 0.0)
        overhangCoefficient = value;
    long integerValue = defaultOptimizationAttempts;
    config.Read("/analysis/optimizationAttempts", &integerValue, defaultOptimizationAttempts);
    if (integerValue > 0 && integerValue <= 100000)
        optimizationAttempts = static_cast<int>(integerValue);
    integerValue = defaultOptimizationWorkers;
    config.Read("/analysis/optimizationWorkers", &integerValue, defaultOptimizationWorkers);
    if (integerValue > 0 && integerValue <= 256)
        optimizationWorkers = static_cast<int>(integerValue);
    value = defaultOptimizationTolerance;
    config.Read("/analysis/optimizationTolerance", &value, defaultOptimizationTolerance);
    if (std::isfinite(value) && value > 0.0)
        optimizationTolerance = value;
    value = defaultSupportSpacing;
    config.Read("/supports/supportSpacing", &value, defaultSupportSpacing);
    if (std::isfinite(value) && value > 0.0)
        supportSpacing = value;
    value = defaultSupportTipTopRadius;
    config.Read("/supports/tipTopRadius", &value, defaultSupportTipTopRadius);
    if (std::isfinite(value) && value > 0.0)
        supportTipTopRadius = value;
    value = defaultSupportTipBottomRadius;
    config.Read("/supports/tipBottomRadius", &value, defaultSupportTipBottomRadius);
    if (std::isfinite(value) && value > 0.0)
        supportTipBottomRadius = value;
    value = defaultSupportTipHeight;
    config.Read("/supports/tipHeight", &value, defaultSupportTipHeight);
    if (std::isfinite(value) && value > 0.0)
        supportTipHeight = value;
    value = defaultSupportLatticeCellSize;
    config.Read("/supports/latticeCellSize", &value, defaultSupportLatticeCellSize);
    if (std::isfinite(value) && value > 0.0)
        supportLatticeCellSize = value;
    value = defaultSupportModelIsolation;
    config.Read("/supports/modelIsolation", &value, defaultSupportModelIsolation);
    if (std::isfinite(value) && value >= 0.0)
        supportModelIsolation = value;
    value = defaultMinimumSupportAngleDegrees;
    config.Read("/supports/minimumSupportAngleDegrees", &value, defaultMinimumSupportAngleDegrees);
    if (std::isfinite(value) && value >= 5.0 && value < 90.0)
        minimumSupportAngleDegrees = value;
    value = defaultSupportBaseHeight;
    config.Read("/supports/baseHeight", &value, defaultSupportBaseHeight);
    if (std::isfinite(value) && value > 0.0)
        supportBaseHeight = value;
    value = defaultSupportBaseRadius;
    config.Read("/supports/baseRadius", &value, defaultSupportBaseRadius);
    if (std::isfinite(value) && value > 0.0)
        supportBaseRadius = value;
    value = defaultSupportPillarBottomRadius;
    config.Read("/supports/pillarBottomRadius", &value, defaultSupportPillarBottomRadius);
    if (std::isfinite(value) && value > 0.0)
        supportPillarBottomRadius = value;
    value = defaultSupportPillarTopRadius;
    config.Read("/supports/pillarTopRadius", &value, defaultSupportPillarTopRadius);
    if (std::isfinite(value) && value > 0.0)
        supportPillarTopRadius = value;
    integerValue = defaultSupportCircumferencePoints;
    config.Read("/supports/circumferencePoints", &integerValue, defaultSupportCircumferencePoints);
    if (integerValue >= 3 && integerValue <= 1024)
        supportCircumferencePoints = static_cast<int>(integerValue);
    value = defaultCrossSectionDisplayDistance;
    config.Read("/interface/crossSectionDisplayDistance",
                &value,
                defaultCrossSectionDisplayDistance);
    if (std::isfinite(value) && value > 0.0)
        crossSectionDisplayDistance = value;
    return true;
}

bool AppSettings::Save() const {
    const wxString directory = settingsDirectory();
    if (!wxDirExists(directory) &&
        !wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL))
        return false;

    wxFileConfig config(
        wxEmptyString, wxEmptyString, FilePath(), wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
    config.Write("/slicing/layerThickness", layerThickness);
    config.Write("/slicing/firstLayerOffset", firstLayerOffset);
    config.Write("/slicing/contourHealingThreshold", contourHealingThreshold);
    config.Write("/slicing/segmentationTolerance", segmentationTolerance);
    config.Write("/analysis/criticalAngleDegrees", criticalAngleDegrees);
    config.Write("/analysis/overhangCoefficient", overhangCoefficient);
    config.Write("/analysis/optimizationAttempts", optimizationAttempts);
    config.Write("/analysis/optimizationWorkers", optimizationWorkers);
    config.Write("/analysis/optimizationTolerance", optimizationTolerance);
    config.Write("/supports/supportSpacing", supportSpacing);
    config.Write("/supports/tipTopRadius", supportTipTopRadius);
    config.Write("/supports/tipBottomRadius", supportTipBottomRadius);
    config.Write("/supports/tipHeight", supportTipHeight);
    config.Write("/supports/latticeCellSize", supportLatticeCellSize);
    config.Write("/supports/modelIsolation", supportModelIsolation);
    config.Write("/supports/minimumSupportAngleDegrees", minimumSupportAngleDegrees);
    config.Write("/supports/baseHeight", supportBaseHeight);
    config.Write("/supports/baseRadius", supportBaseRadius);
    config.Write("/supports/pillarBottomRadius", supportPillarBottomRadius);
    config.Write("/supports/pillarTopRadius", supportPillarTopRadius);
    config.Write("/supports/circumferencePoints", supportCircumferencePoints);
    config.Write("/interface/crossSectionDisplayDistance", crossSectionDisplayDistance);
    return config.Flush();
}
