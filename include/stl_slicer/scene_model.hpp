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

class SceneModelGeometry {
  public:
    SceneModelGeometry(TriangleMesh mesh,
                       Bounds3 bounds,
                       std::vector<RenderVertex> vertices,
                       std::shared_ptr<const SliceData> slices = {});

    const TriangleMesh &triangleMesh() const noexcept {
        return mesh_;
    }
    const Bounds3 &bounds() const noexcept {
        return bounds_;
    }
    const std::vector<RenderVertex> &renderVertices() const noexcept {
        return vertices_;
    }
    const SliceData *slices() const noexcept {
        return slices_.get();
    }

  private:
    TriangleMesh mesh_;
    Bounds3 bounds_;
    std::vector<RenderVertex> vertices_;
    std::shared_ptr<const SliceData> slices_;
};

class SceneModel {
  public:
    virtual ~SceneModel() = default;
    const Bounds3 &localBounds() const {
        return geometry_->bounds();
    }
    const std::vector<RenderVertex> &renderVertices() const {
        return geometry_->renderVertices();
    }
    const TriangleMesh &triangleMesh() const {
        return geometry_->triangleMesh();
    }
    const SceneModelGeometry *geometryIdentity() const noexcept {
        return geometry_.get();
    }
    virtual bool isSliced() const {
        return false;
    }
    virtual const SliceData *slices() const {
        return nullptr;
    }
    virtual std::shared_ptr<SceneModel> replica(std::string modelName) const = 0;

    std::string name;
    std::string sourcePath;
    Mat4 transform;
    bool selected = true;
    bool visible = true;
    Bounds3 worldBounds() const {
        return transformedBounds(localBounds(), transform);
    }

  protected:
    SceneModel(std::string modelName, std::shared_ptr<const SceneModelGeometry> geometry);
    std::shared_ptr<const SceneModelGeometry> geometry_;
};

class MeshSceneModel final : public SceneModel {
  public:
    MeshSceneModel(std::string modelName, TriangleMesh mesh);
    MeshSceneModel(std::string modelName, std::shared_ptr<const SceneModelGeometry> geometry);
    std::shared_ptr<SceneModel> replica(std::string modelName) const override;
};

class SliceSceneModel final : public SceneModel {
  public:
    SliceSceneModel(std::string modelName, SliceData slices);
    SliceSceneModel(std::string modelName, std::shared_ptr<const SceneModelGeometry> geometry);
    bool isSliced() const override {
        return true;
    }
    const SliceData *slices() const override {
        return geometry_->slices();
    }
    std::shared_ptr<SceneModel> replica(std::string modelName) const override;
};

TriangleMesh transformedMesh(const SceneModel &model);

} // namespace stl_slicer
