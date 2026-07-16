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
    return config.Flush();
}
