// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "stl_slicer/model.hpp"
#include "stl_slicer/slice.hpp"
#include "stl_slicer/transform.hpp"
#include <memory>
#include <string>
#include <vector>

namespace stl_slicer {

struct RenderVertex {
    float x, y, z;
    float nx, ny, nz;
};

class SceneModel {
  public:
    virtual ~SceneModel() = default;
    virtual const Bounds3 &localBounds() const = 0;
    virtual const std::vector<RenderVertex> &renderVertices() const = 0;
    virtual bool isSliced() const {
        return false;
    }
    virtual const SliceData *slices() const {
        return nullptr;
    }
    virtual TriangleMesh triangleMesh() const = 0;

    std::string name;
    Mat4 transform;
    bool selected = true;
    bool visible = true;
    Bounds3 worldBounds() const {
        return transformedBounds(localBounds(), transform);
    }
};

class MeshSceneModel final : public SceneModel {
  public:
    MeshSceneModel(std::string modelName, TriangleMesh mesh);
    const Bounds3 &localBounds() const override {
        return mesh_.bounds();
    }
    const std::vector<RenderVertex> &renderVertices() const override {
        return vertices_;
    }
    TriangleMesh triangleMesh() const override;

  private:
    TriangleMesh mesh_;
    std::vector<RenderVertex> vertices_;
};

class SliceSceneModel final : public SceneModel {
  public:
    SliceSceneModel(std::string modelName, SliceData slices);
    const Bounds3 &localBounds() const override {
        return bounds_;
    }
    const std::vector<RenderVertex> &renderVertices() const override {
        return vertices_;
    }
    bool isSliced() const override {
        return true;
    }
    const SliceData *slices() const override {
        return &slices_;
    }
    TriangleMesh triangleMesh() const override;

  private:
    void buildMesh();
    SliceData slices_;
    Bounds3 bounds_;
    std::vector<RenderVertex> vertices_;
};

TriangleMesh transformedMesh(const SceneModel &model);

} // namespace stl_slicer
