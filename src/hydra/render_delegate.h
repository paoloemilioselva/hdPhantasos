#pragma once

#include "api.h"
#include "render_param.h"
#include "scene_store.h"
#include "worker_link.h"

#include "pxr/base/vt/dictionary.h"
#include "pxr/imaging/hd/renderDelegate.h"

#include <memory>
#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE

/// Owns the scene store for the lifetime of one render index. Everything the
/// delegate draws is a mesh: implicit surfaces arrive as meshes through the
/// scene index plugin, and there are no materials or lights, because the
/// proxy is flat colour by design.
class HDPHANTASOS_API HdPhantasosRenderDelegate final : public HdRenderDelegate {
  public:
    HdPhantasosRenderDelegate();
    explicit HdPhantasosRenderDelegate(const HdRenderSettingsMap& settingsMap);
    ~HdPhantasosRenderDelegate() override;

    const TfTokenVector& GetSupportedRprimTypes() const override;
    const TfTokenVector& GetSupportedSprimTypes() const override;
    const TfTokenVector& GetSupportedBprimTypes() const override;

    HdRenderParam* GetRenderParam() const override;
    HdResourceRegistrySharedPtr GetResourceRegistry() const override;

    HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex* index,
                                           const HdRprimCollection& collection) override;

    HdInstancer* CreateInstancer(HdSceneDelegate* delegate, const SdfPath& id) override;
    void DestroyInstancer(HdInstancer* instancer) override;

    HdRprim* CreateRprim(const TfToken& typeId, const SdfPath& rprimId) override;
    void DestroyRprim(HdRprim* rprim) override;

    HdSprim* CreateSprim(const TfToken& typeId, const SdfPath& sprimId) override;
    HdSprim* CreateFallbackSprim(const TfToken& typeId) override;
    void DestroySprim(HdSprim* sprim) override;

    HdBprim* CreateBprim(const TfToken& typeId, const SdfPath& bprimId) override;
    HdBprim* CreateFallbackBprim(const TfToken& typeId) override;
    void DestroyBprim(HdBprim* bprim) override;

    void CommitResources(HdChangeTracker* tracker) override;

    HdAovDescriptor GetDefaultAovDescriptor(const TfToken& name) const override;
    HdRenderSettingDescriptorList GetRenderSettingDescriptors() const override;
    VtDictionary GetRenderStats() const override;

    // --- Used by the render pass ----------------------------------------------

    HdPhantasosSceneStore* Store() const { return _store.get(); }

    /// The model worker, started on first use. Made again when 'Allow
    /// downloads' changes, since the worker is started with it.
    HdPhantasosWorkerLink* WorkerLink();

    /// What the last frame cost and showed, for GetRenderStats.
    void RecordFrame(const VtDictionary& stats);

  private:
    void _Initialize();

    std::unique_ptr<HdPhantasosSceneStore> _store;
    std::unique_ptr<HdPhantasosRenderParam> _renderParam;
    HdResourceRegistrySharedPtr _resourceRegistry;
    HdRenderSettingDescriptorList _settingDescriptors;

    mutable std::mutex _statsMutex;
    VtDictionary _frameStats;

    std::mutex _workerMutex;
    std::unique_ptr<HdPhantasosWorkerLink> _worker;
    bool _workerAllowsDownload = false;
};

PXR_NAMESPACE_CLOSE_SCOPE
