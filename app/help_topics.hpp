// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

class wxWindow;

namespace clip_slicer::help {

inline constexpr char manualTop[] = "manual-top";
inline constexpr char documentWindow[] = "document-window";
inline constexpr char documentMenus[] = "document-menus";
inline constexpr char documentToolbar[] = "document-toolbar";
inline constexpr char modelList[] = "model-list";
inline constexpr char viewport[] = "viewport";
inline constexpr char sectionControls[] = "section-controls";
inline constexpr char settingsDialog[] = "settings-dialog";
inline constexpr char settingsSlicing[] = "settings-slicing";
inline constexpr char settingsAnalysis[] = "settings-analysis";
inline constexpr char settingsGenerator[] = "settings-generator";
inline constexpr char settingsInterface[] = "settings-interface";
inline constexpr char transformDialog[] = "transform-dialog";
inline constexpr char sliceDialog[] = "slice-dialog";
inline constexpr char sliceOutput[] = "slice-output";
inline constexpr char sectionDialog[] = "section-dialog";
inline constexpr char sectionAxis[] = "section-axis";
inline constexpr char sectionClipping[] = "section-clipping";
inline constexpr char sectionAutoRotate[] = "section-auto-rotate";
inline constexpr char openModelDialog[] = "open-model-dialog";
inline constexpr char openIntoDocumentDialog[] = "open-into-document-dialog";
inline constexpr char exportSlicesDialog[] = "export-slices-dialog";
inline constexpr char exportStlDialog[] = "export-stl-dialog";

// Stores the stable manual anchor on a window or control.
void Assign(wxWindow *window, const char *topic);

// Routes F1, native context-help events, and wxID_HELP buttons to the nearest
// HelpText value on the focused control's parent chain.
void Enable(wxWindow *root);

} // namespace clip_slicer::help
