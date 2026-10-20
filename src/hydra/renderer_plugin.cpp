#include "renderer_plugin.h"

#include "render_delegate.h"
#include "tokens.h"

#include "pxr/imaging/hd/rendererPluginRegistry.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdPhantasosTokens, HDPHANTASOS_TOKENS);

TF_REGISTRY_FUNCTION(TfType)
{
    HdRendererPluginRegistry::Define<HdPhantasosRendererPlugin>();
}

HdRenderDelegate* HdPhantasosRendererPlugin::CreateRenderDelegate()
{
    return new HdPhantasosRenderDelegate();
}

HdRenderDelegate* HdPhantasosRendererPlugin::CreateRenderDelegate(
    const HdRenderSettingsMap& settingsMap)
{
    return new HdPhantasosRenderDelegate(settingsMap);
}

void HdPhantasosRendererPlugin::DeleteRenderDelegate(HdRenderDelegate* renderDelegate)
{
    delete renderDelegate;
}

bool HdPhantasosRendererPlugin::IsSupported(const HdRendererCreateArgs& /*createArgs*/,
                                            std::string* /*reasonWhyNot*/) const
{
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
