#pragma once

#include "scene_store.h"

#include "pxr/imaging/hd/mesh.h"

#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

/// Publishes a Hydra mesh to the scene store: its triangles, its placements,
/// and the phantasos:* primvars that say what it is.
///
/// Every implicit surface (cube, sphere, cylinder, ...) arrives here as a mesh
/// too, converted by the scene index plugin.
class HdPhantasosMesh final : public HdMesh {
  public:
    explicit HdPhantasosMesh(const SdfPath& id);
    ~HdPhantasosMesh() override;

    HdDirtyBits GetInitialDirtyBitsMask() const override;

    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits, const TfToken& reprToken) override;

    void Finalize(HdRenderParam* renderParam) override;

  protected:
    void _InitRepr(const TfToken& reprToken, HdDirtyBits* dirtyBits) override;
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

  private:
    /// Kept between syncs so an edit to one part reuses the rest.
    HdPhantasosMeshRecord _record;
};

PXR_NAMESPACE_CLOSE_SCOPE
