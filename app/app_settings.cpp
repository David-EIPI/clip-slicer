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
    contourHealingThreshold = defaultContourHealingThreshold;
    segmentationTolerance = defaultSegmentationTolerance;
    if (!wxFileExists(FilePath()))
        return true;

    wxFileConfig config(
        wxEmptyString, wxEmptyString, FilePath(), wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
    double value = defaultContourHealingThreshold;
    config.Read("/slicing/contourHealingThreshold", &value, defaultContourHealingThreshold);
    if (std::isfinite(value) && value > 0.0)
        contourHealingThreshold = value;
    value = defaultSegmentationTolerance;
    config.Read("/slicing/segmentationTolerance", &value, defaultSegmentationTolerance);
    if (std::isfinite(value) && value > 0.0)
        segmentationTolerance = value;
    return true;
}

bool AppSettings::Save() const {
    const wxString directory = settingsDirectory();
    if (!wxDirExists(directory) &&
        !wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL))
        return false;

    wxFileConfig config(
        wxEmptyString, wxEmptyString, FilePath(), wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
    config.Write("/slicing/contourHealingThreshold", contourHealingThreshold);
    config.Write("/slicing/segmentationTolerance", segmentationTolerance);
    return config.Flush();
}
