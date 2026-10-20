#pragma once

// Spheres, cubes, cones, cylinders, capsules and planes, turned into meshes.
//
// `UsdGeomCube` and its siblings reach a render delegate as their own prim
// types, not as meshes, and OpenUSD inserts `HdsiImplicitSurfaceSceneIndex`
// for nobody: each renderer inserts it for itself (hdClaude's
// implicit_surface_scene_index_plugin.h tells what happens otherwise -- the
// prims are silently absent). Phantasos stages are built from exactly these
// prims, so every type becomes a mesh, the sphere included.

#include "pxr/imaging/hd/sceneIndexPlugin.h"
#include "pxr/pxr.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdPhantasos_ImplicitSurfaceSceneIndexPlugin final : public HdSceneIndexPlugin {
  public:
    HdPhantasos_ImplicitSurfaceSceneIndexPlugin();

  protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr& inputScene,
        const HdContainerDataSourceHandle& inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE
