#include "render_delegate.h"

#include "instancer.h"
#include "mesh.h"
#include "render_buffer.h"
#include "render_pass.h"
#include "tokens.h"

#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/tf/getenv.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/imaging/hd/bprim.h"
#include "pxr/imaging/hd/camera.h"
#include "pxr/imaging/hd/resourceRegistry.h"
#include "pxr/imaging/hd/tokens.h"

#include <cstdlib>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

float EnvFloat(const char* name, float fallback)
{
    const std::string text = TfGetenv(name);
    if (text.empty()) return fallback;
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    return end != text.c_str() ? value : fallback;
}

bool EnvBool(const char* name, bool fallback)
{
    const std::string text = TfGetenv(name);
    if (text.empty()) return fallback;
    return text == "1" || text == "true" || text == "on" || text == "yes";
}

std::string EnvString(const char* name, const char* fallback)
{
    const std::string text = TfGetenv(name);
    return text.empty() ? fallback : text;
}

/// The worker's code, installed beside the plugin's plugInfo.json.
std::string WorkerDirectory()
{
    if (const PlugPluginPtr plugin = PlugRegistry::GetInstance().GetPluginWithName("hdPhantasos")) {
        return TfStringCatPaths(plugin->GetResourcePath(), "worker");
    }
    return {};
}

}  // namespace

HdPhantasosWorkerLink* HdPhantasosRenderDelegate::WorkerLink()
{
    const bool allowDownload = GetRenderSetting<bool>(HdPhantasosTokens->allowDownload, false);
    std::lock_guard<std::mutex> lock(_workerMutex);
    if (!_worker || allowDownload != _workerAllowsDownload) {
        _worker.reset();
        HdPhantasosWorkerLink::Options options;
        options.cache = HdPhantasosWorkerLink::DefaultCache();
        options.workerDirectory = WorkerDirectory();
        // A Python of one's own instead of the venv's: for tests, which run
        // the echo model with nothing but Pillow.
        options.python = TfGetenv("HDPHANTASOS_WORKER_PYTHON");
        options.allowDownload = allowDownload;
        _worker = std::make_unique<HdPhantasosWorkerLink>(options);
        _workerAllowsDownload = allowDownload;
    }
    return _worker.get();
}

HdPhantasosRenderDelegate::HdPhantasosRenderDelegate() : HdRenderDelegate()
{
    _Initialize();
}

HdPhantasosRenderDelegate::HdPhantasosRenderDelegate(const HdRenderSettingsMap& settingsMap)
    : HdRenderDelegate(settingsMap)
{
    _Initialize();
}

HdPhantasosRenderDelegate::~HdPhantasosRenderDelegate() = default;

void HdPhantasosRenderDelegate::_Initialize()
{
    _store = std::make_unique<HdPhantasosSceneStore>();
    _renderParam = std::make_unique<HdPhantasosRenderParam>(_store.get());
    _resourceRegistry = std::make_shared<HdResourceRegistry>();

    // Environment variables supply the defaults, so usdrecord, which has no
    // settings UI, can set them; render_phantasos.bat lists the same names. A
    // host that sets a value explicitly wins: _PopulateDefaultSettings fills
    // only the keys the host left unset.
    _settingDescriptors = {
        {"Outline width (pixels at 480 rows)", HdPhantasosTokens->outlineWidth,
         VtValue(EnvFloat("HDPHANTASOS_OUTLINE_WIDTH", 1.0f))},
        {"Shade the proxy (light fixed to the camera)", HdPhantasosTokens->proxyShading,
         VtValue(EnvBool("HDPHANTASOS_PROXY_SHADING", false))},
        {"Clay (one grey material, shaded)", HdPhantasosTokens->proxyClay,
         VtValue(EnvBool("HDPHANTASOS_PROXY_CLAY", false))},
        {"Ground checker size (world units)", HdPhantasosTokens->checkerSize,
         VtValue(EnvFloat("HDPHANTASOS_CHECKER_SIZE", 1.0f))},
        {"Generate pictures (model worker)", HdPhantasosTokens->generate,
         VtValue(EnvBool("HDPHANTASOS_GENERATE", false))},
        {"Model (klein4b, matrixgame, echo)", HdPhantasosTokens->model,
         VtValue(EnvString("HDPHANTASOS_MODEL", "klein4b"))},
        {"Stream's first frame (model, klein, release)", HdPhantasosTokens->firstFrame,
         VtValue(EnvString("HDPHANTASOS_FIRST_FRAME", "model"))},
        {"Picture size (long side, pixels)", HdPhantasosTokens->generateSize,
         VtValue(static_cast<int>(EnvFloat("HDPHANTASOS_GENERATE_SIZE", 512)))},
        {"Seed", HdPhantasosTokens->seed, VtValue(static_cast<int>(EnvFloat("HDPHANTASOS_SEED", 0)))},
        {"Style (added to the prompt)", HdPhantasosTokens->style,
         VtValue(EnvString("HDPHANTASOS_STYLE", ""))},
        {"Allow downloads (worker setup, model weights)", HdPhantasosTokens->allowDownload,
         VtValue(EnvBool("HDPHANTASOS_ALLOW_DOWNLOAD", false))},
    };
    _PopulateDefaultSettings(_settingDescriptors);
}

const TfTokenVector& HdPhantasosRenderDelegate::GetSupportedRprimTypes() const
{
    static const TfTokenVector types = {HdPrimTypeTokens->mesh};
    return types;
}

const TfTokenVector& HdPhantasosRenderDelegate::GetSupportedSprimTypes() const
{
    static const TfTokenVector types = {HdPrimTypeTokens->camera};
    return types;
}

const TfTokenVector& HdPhantasosRenderDelegate::GetSupportedBprimTypes() const
{
    static const TfTokenVector types = {HdPrimTypeTokens->renderBuffer};
    return types;
}

HdRenderParam* HdPhantasosRenderDelegate::GetRenderParam() const
{
    return _renderParam.get();
}

HdResourceRegistrySharedPtr HdPhantasosRenderDelegate::GetResourceRegistry() const
{
    return _resourceRegistry;
}

HdRenderPassSharedPtr HdPhantasosRenderDelegate::CreateRenderPass(
    HdRenderIndex* index, const HdRprimCollection& collection)
{
    return std::make_shared<HdPhantasosRenderPass>(index, collection, this);
}

HdInstancer* HdPhantasosRenderDelegate::CreateInstancer(HdSceneDelegate* delegate,
                                                        const SdfPath& id)
{
    return new HdPhantasosInstancer(delegate, id);
}

void HdPhantasosRenderDelegate::DestroyInstancer(HdInstancer* instancer)
{
    delete instancer;
}

HdRprim* HdPhantasosRenderDelegate::CreateRprim(const TfToken& typeId, const SdfPath& rprimId)
{
    if (typeId == HdPrimTypeTokens->mesh) return new HdPhantasosMesh(rprimId);
    TF_CODING_ERROR("hdPhantasos: unknown rprim type %s", typeId.GetText());
    return nullptr;
}

void HdPhantasosRenderDelegate::DestroyRprim(HdRprim* rprim)
{
    delete rprim;
}

HdSprim* HdPhantasosRenderDelegate::CreateSprim(const TfToken& typeId, const SdfPath& sprimId)
{
    if (typeId == HdPrimTypeTokens->camera) return new HdCamera(sprimId);
    TF_CODING_ERROR("hdPhantasos: unknown sprim type %s", typeId.GetText());
    return nullptr;
}

HdSprim* HdPhantasosRenderDelegate::CreateFallbackSprim(const TfToken& typeId)
{
    if (typeId == HdPrimTypeTokens->camera) return new HdCamera(SdfPath::EmptyPath());
    return nullptr;
}

void HdPhantasosRenderDelegate::DestroySprim(HdSprim* sprim)
{
    delete sprim;
}

HdBprim* HdPhantasosRenderDelegate::CreateBprim(const TfToken& typeId, const SdfPath& bprimId)
{
    if (typeId == HdPrimTypeTokens->renderBuffer) return new HdPhantasosRenderBuffer(bprimId);
    TF_CODING_ERROR("hdPhantasos: unknown bprim type %s", typeId.GetText());
    return nullptr;
}

HdBprim* HdPhantasosRenderDelegate::CreateFallbackBprim(const TfToken& typeId)
{
    if (typeId == HdPrimTypeTokens->renderBuffer) {
        return new HdPhantasosRenderBuffer(SdfPath::EmptyPath());
    }
    return nullptr;
}

void HdPhantasosRenderDelegate::DestroyBprim(HdBprim* bprim)
{
    delete bprim;
}

void HdPhantasosRenderDelegate::CommitResources(HdChangeTracker* /*tracker*/) {}

HdAovDescriptor HdPhantasosRenderDelegate::GetDefaultAovDescriptor(const TfToken& name) const
{
    // Colour is linear float: the proxy palette is sRGB, converted to linear
    // so that a host's display transform shows exactly the palette colours.
    if (name == HdAovTokens->color) {
        return HdAovDescriptor(HdFormatFloat32Vec4, false, VtValue(GfVec4f(0, 0, 0, 1)));
    }
    if (name == HdAovTokens->depth) {
        return HdAovDescriptor(HdFormatFloat32, false, VtValue(1.0f));
    }
    if (name == HdAovTokens->normal) {
        return HdAovDescriptor(HdFormatFloat32Vec3, false, VtValue(GfVec3f(0.0f)));
    }
    if (name == HdAovTokens->primId || name == HdAovTokens->instanceId ||
        name == HdAovTokens->elementId || name == HdPhantasosTokens->subjectId) {
        return HdAovDescriptor(HdFormatInt32, false, VtValue(-1));
    }
    return HdAovDescriptor();
}

HdRenderSettingDescriptorList HdPhantasosRenderDelegate::GetRenderSettingDescriptors() const
{
    return _settingDescriptors;
}

void HdPhantasosRenderDelegate::RecordFrame(const VtDictionary& stats)
{
    std::lock_guard<std::mutex> lock(_statsMutex);
    _frameStats = stats;
}

VtDictionary HdPhantasosRenderDelegate::GetRenderStats() const
{
    std::lock_guard<std::mutex> lock(_statsMutex);
    return _frameStats;
}

PXR_NAMESPACE_CLOSE_SCOPE
