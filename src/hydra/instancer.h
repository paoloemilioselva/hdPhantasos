#pragma once

// Point and native instancing.
//
// `HdInstancer` holds the primvars and the parent chain and computes nothing,
// so the delegate composes each instance's transform itself, as hdClaude does
// (and for the reason its instancer.h records: `GetInstancerTransforms`
// returns the instancer's own transform, not one per instance).

#include "pxr/base/tf/hashmap.h"
#include "pxr/base/vt/types.h"
#include "pxr/base/vt/value.h"
#include "pxr/imaging/hd/instancer.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdPhantasosInstancer final : public HdInstancer {
  public:
    HdPhantasosInstancer(HdSceneDelegate* delegate, const SdfPath& id);
    ~HdPhantasosInstancer() override;

    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits) override;

    /// One world transform per instance of `prototypeId`: the instancer's own
    /// transform, then translate, rotate and scale from its primvars, then the
    /// per-instance `instanceTransform`. Nested instancers multiply.
    VtMatrix4dArray ComputeInstanceTransforms(const SdfPath& prototypeId);

  private:
    TfHashMap<TfToken, VtValue, TfToken::HashFunctor> _primvars;
};

PXR_NAMESPACE_CLOSE_SCOPE
