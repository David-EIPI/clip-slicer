# STL Slicer

A dependency-free C++17 library and command-line program that reads binary STL triangle meshes,
intersects them with horizontal planes, connects the segments into contours, and writes Common
Layer Interface (CLI) files.

The repository also contains **CLIP Slicer**, a wxWidgets MDI desktop application for loading,
viewing, transforming, slicing, and exporting STL and CLI models.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The GUI build requires wxWidgets 3.2, OpenGL, GLU, and libepoxy. It is built by default when those
development packages are available:

```sh
./build/clip-slicer
./build/clip-slicer model.stl
```

Set `-DSTL_SLICER_BUILD_GUI=OFF` for a command-line-only build.

If the host uses `ccache` with an unavailable cache directory, prefix these commands with
`CCACHE_DISABLE=1`.

To enable compile-time CPU-specific vectorization, set the compiler architecture target:

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release -DSTL_SLICER_ARCH=native
cmake --build build-native
```

The target is intentionally opt-in so normal release binaries remain portable. A named GCC target
such as `core-avx-i` can be used instead of `native` when building for another machine. GCC
vectorization decisions and source-interleaved assembly can be inspected with:

```sh
c++ -std=c++17 -O3 -march=native -fopt-info-vec-all=vectorization.txt \
  -S -g -fverbose-asm -Iinclude src/slicer.cpp -o slicer.s
```

## Use

```sh
./build/stl-slicer model.stl layers.cli 0.1
./build/stl-slicer --ascii --tolerance 0.00001 model.stl layers.cli 0.1
./build/stl-slicer --gap-multiplier 2 model.stl layers.cli 0.1
```

The layer thickness defaults to 0.1 mm. Binary CLI output using PolyLine Long commands is the
default; `--ascii` is useful for inspection. STL coordinates are treated as millimeters because
STL itself does not store units. When writing CLI geometry, layer Z coordinates are translated so
the lowest generated layer is positioned at half the layer thickness above the build platform.
X and Y coordinates are translated by their minimum emitted values. The resulting CLI model has
its bounding-box origin at `(0, 0, 0)`, while the reusable slice objects retain their original
model-space coordinates. CLI `DIMENSION` metadata describes this translated geometry and the full
layer-stack height.

After exact segment connection, the slicer conservatively heals endpoints of paths that remain
open when their gap is no more than `join tolerance * gap multiplier`. The multiplier defaults to
2.0. Use `--gap-multiplier 1` to prevent the secondary pass from accepting wider gaps, or reduce
`--tolerance` when a model intentionally contains features separated by only a few micrometers.

The writer includes the custom header directive `$$USERDATA/CLIPSlicer,0,`. This compatibility
instruction tells the target third-party reader to join open contours automatically and determine
filled and empty regions from the winding rule. It is emitted for both ASCII and binary CLI files.

The library API is divided into reusable data and processing components:

- `TriangleMesh`, `Triangle`, `Vec2`, `Vec3`, and `Bounds3` store model geometry.
- `BinaryStlReader` validates and reads little-endian binary STL streams or files.
- `Slicer` produces reusable `SliceData`, `SliceLayer`, and `SlicePath` objects.
- `CliWriter` writes binary or ASCII CLI streams or files.
- `CliReader` reads binary CLI layer files.
- `SceneModel`, `MeshSceneModel`, and `SliceSceneModel` provide shared transformed/renderable
  model abstractions for applications.

## CLIP Slicer controls

- Left drag rotates the camera around the models.
- Middle drag translates the camera parallel to the screen; right drag moves it along the screen
  normal.
- Mouse wheel zooms the camera view. Ctrl+left drag duplicates wheel zoom, and Ctrl+right drag
  duplicates middle drag.
- Holding Shift with a mouse operation applies the corresponding rotation, translation, or scaling
  to selected models instead of the camera.
- Interactive slicing adds a translucent plane; Alt+wheel moves it and updates the projected
  contour and status-bar area. Alt+Shift+left drag provides the same control without a wheel.

`File > Open...` creates a new document, while `File > Open into document...` adds a model to the
active document. Slice export merges every selected sliced model and stably orders their layers by
Z, including layers from different models at the same height.

The viewport draws global X/Y/Z axes in both directions. Cross-tick spacing follows viewport scale
in power-of-ten steps with a 2x density adjustment. A fixed-size orientation vane in the lower-left
uses blue for X, red for Y, and bright green for Z.

Model transforms are matrix-based and do not modify source coordinates. Slicing applies the stored
transforms to a temporary triangle mesh. Render normals are blended across non-crease edges. Sliced
models use GLU winding-rule tessellation for concave top and bottom caps, including holes. Side and
cap geometry are cached in static OpenGL vertex buffers and rebuilt only when model geometry
changes.

Closed contours are classified by containment depth. External contours are emitted
counter-clockwise (`dir = 1`), internal contours clockwise (`dir = 0`), and unconnected paths as
open (`dir = 2`). Coplanar horizontal triangles are ignored using a consistent half-open plane
intersection rule.

## Current scope

The slicer assumes a clean, watertight triangle mesh for closed output. Endpoint tolerance can
bridge small numeric discrepancies, but this version does not repair holes, self-intersections,
overlapping shells, or non-manifold geometry.

Sliced-model rendering currently builds the vertical prism facets. Robust triangulation of concave
top/bottom surfaces, contour editing tools, settings persistence, and cross-layer exposed-surface
optimization remain planned geometry/editor work.
