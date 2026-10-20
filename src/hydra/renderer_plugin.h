#pragma once

#include "api.h"

#include "pxr/imaging/hd/rendererPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

/// Entry point Hydra discovers through plugInfo.json.
class HDPHANTASOS_API HdPhantasosRendererPlugin final : public HdRendererPlugin {
  public:
    HdPhantasosRendererPlugin() = default;
    ~HdPhantasosRendererPlugin() override = default;

    HdRenderDelegate* CreateRenderDelegate() override;
    HdRenderDelegate* CreateRenderDelegate(
        const HdRenderSettingsMap& settingsMap) override;
    void DeleteRenderDelegate(HdRenderDelegate* renderDelegate) override;

    /// Always: the rasterizer runs on the CPU and needs no device.
    bool IsSupported(const HdRendererCreateArgs& createArgs,
                     std::string* reasonWhyNot = nullptr) const override;
};

PXR_NAMESPACE_CLOSE_SCOPE
