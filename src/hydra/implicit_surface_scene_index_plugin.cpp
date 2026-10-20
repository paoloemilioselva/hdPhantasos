#include "implicit_surface_scene_index_plugin.h"

#include "tokens.h"

#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hdsi/implicitSurfaceSceneIndex.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens,
                         ((sceneIndexPluginName, "HdPhantasos_ImplicitSurfaceSceneIndexPlugin")));

TF_REGISTRY_FUNCTION(TfType)
{
    HdSceneIndexPluginRegistry::Define<HdPhantasos_ImplicitSurfaceSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    // Phase 0, as hdStorm and hdEmbree place it.
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;
    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        kHdPhantasosDisplayName, _tokens->sceneIndexPluginName,
        /* inputArgs = */ nullptr, insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

HdPhantasos_ImplicitSurfaceSceneIndexPlugin::HdPhantasos_ImplicitSurfaceSceneIndexPlugin() =
    default;

HdSceneIndexBaseRefPtr HdPhantasos_ImplicitSurfaceSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr& inputScene, const HdContainerDataSourceHandle& /*inputArgs*/)
{
    const HdDataSourceBaseHandle toMesh = HdRetainedTypedSampledDataSource<TfToken>::New(
        HdsiImplicitSurfaceSceneIndexTokens->toMesh);
    const HdContainerDataSourceHandle arguments = HdRetainedContainerDataSource::New(
        HdPrimTypeTokens->sphere, toMesh, HdPrimTypeTokens->cube, toMesh,
        HdPrimTypeTokens->cone, toMesh, HdPrimTypeTokens->cylinder, toMesh,
        HdPrimTypeTokens->capsule, toMesh, HdPrimTypeTokens->plane, toMesh);
    return HdsiImplicitSurfaceSceneIndex::New(inputScene, arguments);
}

PXR_NAMESPACE_CLOSE_SCOPE
