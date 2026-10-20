#pragma once

#include "hdphantasos/core/raster.h"

#include "pxr/base/tf/token.h"
#include "pxr/usd/sdf/path.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// A mesh's triangles in object space. Shared, immutable, and replaced only
/// when points or topology change, so a transform edit reuses it.
struct HdPhantasosGeometry {
    std::vector<float> points;            // xyz
    std::vector<std::int32_t> triangles;  // three point indices each
    std::vector<std::int32_t> faces;      // the coarse face each triangle came from
};

/// What the store holds for one mesh: everything the render pass needs, and
/// nothing that refers back into Hydra.
struct HdPhantasosMeshRecord {
    std::shared_ptr<const HdPhantasosGeometry> geometry;
    /// One world transform per placement; one entry for an uninstanced mesh.
    std::vector<phantasos::Matrix4> placements;
    bool instanced = false;
    bool visible = true;
    /// The mesh's path. Its render tag is looked up from the render index at
    /// draw time: Hydra updates a tag without syncing the prim.
    SdfPath id;
    std::int32_t primId = -1;
    /// The subject this mesh belongs to. `subject.key` is the authored
    /// phantasos:subject, or the mesh's own path when there is none.
    phantasos::Subject subject;
    /// phantasos:shot, the whole picture's description, usually authored on
    /// the root prim and so inherited by every mesh.
    std::string shot;
};

/// The scene as the meshes publish it during Sync, read by the render pass.
///
/// Sync runs meshes in parallel, so every access is under one mutex. The
/// revision changes on every publish, and is how the render pass knows a
/// frame it already drew is still the right one.
class HdPhantasosSceneStore {
  public:
    void Set(const SdfPath& id, std::shared_ptr<const HdPhantasosMeshRecord> record);
    void Remove(const SdfPath& id);

    /// Every mesh, in path order, so the subject numbering and the picture do
    /// not depend on the order Hydra synced in.
    std::vector<std::shared_ptr<const HdPhantasosMeshRecord>> Snapshot(
        std::uint64_t* revision) const;

    std::uint64_t Revision() const;

  private:
    mutable std::mutex _mutex;
    std::map<SdfPath, std::shared_ptr<const HdPhantasosMeshRecord>> _meshes;
    std::uint64_t _revision = 1;
};

PXR_NAMESPACE_CLOSE_SCOPE
