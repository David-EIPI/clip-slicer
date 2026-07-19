<!-- SPDX-FileCopyrightText: 2026 David Shirvanyants -->
<!-- SPDX-License-Identifier: CC0-1.0 -->

# CLIP Slicer Licensing

This repository contains material under several licenses. The license that
applies depends on the kind of material, as described below.

## Source code

The CLIP Slicer source code is licensed under the
[PolyForm Noncommercial License 1.0.0](LICENSES/PolyForm-Noncommercial-1.0.0.md).
This includes the C++ sources, headers, tests, build scripts, and packaging
files, except for embedded artwork explicitly identified as CC0.

Required Notice: Copyright 2026 David Shirvanyants (https://www.linkedin.com/in/david-shirvanyants)

The public license permits the uses described in the PolyForm Noncommercial
terms. Commercial use is not granted by that license. Commercial use requires
explicit written permission from David Shirvanyants, who can be contacted via
the LinkedIn profile above.

## Documentation and original artwork

Original project documentation and original artwork are made available under
[CC0 1.0 Universal](LICENSES/CC0-1.0.txt). This includes `README.md`, the
documentation sources under `docs/`, toolbar icons under `app/assets/`, and
their embedded byte-array representations. Build scripts, including
`docs/Makefile`, remain source code under PolyForm Noncommercial.

CC0 applies only to rights held by David Shirvanyants. It does not override
rights in third-party material.

## Screenshot exception and attribution

The following screenshots are excluded from the CC0 dedication:

- `images/screenshot-model.png`
- `images/screenshot-supports.png`

They depict the **Bust of Sappho** model by
[LukeChilson](https://www.thingiverse.com/LukeChilson), obtained from
[Thingiverse thing 14565](https://www.thingiverse.com/thing:14565). Thingiverse
identifies that model as licensed under
[Creative Commons Attribution-ShareAlike 4.0 International](LICENSES/CC-BY-SA-4.0.txt).
The screenshots, as adaptations incorporating that model, are distributed
under CC BY-SA 4.0 as well.

## Other third-party material

- `STL_(file_format)` is a saved Wikipedia reference and remains governed by
  the CC BY-SA 4.0 terms and attribution information contained in that file.
- `cli_format.html` is a saved external CLI-format reference. It is not covered
  by the project license grants; its source URL is recorded in the file. Its
  redistribution terms have not been identified and should be confirmed, or
  the saved copy removed, before a public source release.
- wxWidgets, Clipper2, and any other third-party dependencies retain their own
  licenses. Nothing in this repository relicenses those components.
