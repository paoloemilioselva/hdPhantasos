#pragma once

#include "scene_store.h"

#include "pxr/imaging/hd/renderDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

/// What prims reach during Sync: the store they publish into.
class HdPhantasosRenderParam final : public HdRenderParam {
  public:
    explicit HdPhantasosRenderParam(HdPhantasosSceneStore* store) : _store(store) {}
    HdPhantasosSceneStore* Store() const { return _store; }

  private:
    HdPhantasosSceneStore* _store;
};

PXR_NAMESPACE_CLOSE_SCOPE
