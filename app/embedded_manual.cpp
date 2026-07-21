// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "embedded_manual.hpp"

namespace clip_slicer::assets {

const unsigned char manualHtml[] = {
#include "embedded_manual.inc"
};
const std::size_t manualHtmlSize = sizeof(manualHtml);

} // namespace clip_slicer::assets
