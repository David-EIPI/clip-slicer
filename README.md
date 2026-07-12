# STL Slicer

A dependency-free C++17 library and command-line program that reads binary STL triangle meshes,
intersects them with horizontal planes, connects the segments into contours, and writes Common
Layer Interface (CLI) files.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

If the host uses `ccache` with an unavailable cache directory, prefix these commands with
`CCACHE_DISABLE=1`.

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

Closed contours are classified by containment depth. External contours are emitted
counter-clockwise (`dir = 1`), internal contours clockwise (`dir = 0`), and unconnected paths as
open (`dir = 2`). Coplanar horizontal triangles are ignored using a consistent half-open plane
intersection rule.

## Current scope

The slicer assumes a clean, watertight triangle mesh for closed output. Endpoint tolerance can
bridge small numeric discrepancies, but this version does not repair holes, self-intersections,
overlapping shells, or non-manifold geometry. Segment connection is intentionally straightforward;
large production meshes would benefit from spatial indexing and active-triangle bucketing.
