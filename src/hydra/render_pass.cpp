#include "render_pass.h"

#include "render_buffer.h"
#include "render_delegate.h"
#include "tokens.h"
#include "trace.h"

#include "pxr/base/tf/getenv.h"
#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/imaging/hd/renderPassState.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hio/image.h"

#include <algorithm>
#include <fstream>
#include <unordered_map>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

phantasos::Matrix4 ToMatrix(const GfMatrix4d& m)
{
    phantasos::Matrix4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) r.m[i][j] = m[i][j];
    return r;
}

/// Writes what the frame shows, for looking at and, later, for the worker:
/// manifest.json (the subjects, their descriptions and where they are) and
/// proxy.png (the guide image), into HDPHANTASOS_MANIFEST when it names a
/// directory. Each frame overwrites the last.
void WriteManifest(const std::string& directory, const std::string& json,
                   const phantasos::Frame& frame)
{
    if (!TfIsDir(directory) && !TfMakeDirs(directory, -1, true)) {
        TF_WARN("hdPhantasos: cannot create the manifest directory '%s'", directory.c_str());
        return;
    }
    std::ofstream(TfStringCatPaths(directory, "manifest.json"), std::ios::binary) << json;

    std::vector<std::uint8_t> rgb = phantasos::ProxyRgb8(frame);
    HioImageSharedPtr image = HioImage::OpenForWriting(TfStringCatPaths(directory, "proxy.png"));
    if (!image) return;
    HioImage::StorageSpec storage;
    storage.width = frame.width;
    storage.height = frame.height;
    storage.format = HioFormatUNorm8Vec3srgb;
    storage.flipped = false;
    storage.data = rgb.data();
    if (!image->Write(storage)) {
        TF_WARN("hdPhantasos: could not write the proxy image into '%s'", directory.c_str());
    }
}

}  // namespace

bool HdPhantasosRenderPass::Framing::operator==(const Framing& other) const
{
    return worldToView == other.worldToView && projection == other.projection &&
           width == other.width && height == other.height &&
           sceneRevision == other.sceneRevision && settingsVersion == other.settingsVersion &&
           renderTags == other.renderTags;
}

HdPhantasosRenderPass::HdPhantasosRenderPass(HdRenderIndex* index,
                                             const HdRprimCollection& collection,
                                             HdPhantasosRenderDelegate* renderDelegate)
    : HdRenderPass(index, collection), _renderDelegate(renderDelegate)
{
}

HdPhantasosRenderPass::~HdPhantasosRenderPass() = default;

void HdPhantasosRenderPass::_Execute(const HdRenderPassStateSharedPtr& renderPassState,
                                     const TfTokenVector& renderTags)
{
    if (!_renderDelegate || !renderPassState) return;

    // --- Which AOVs, and at what size: the colour buffer's, or the first.
    struct Target {
        HdPhantasosRenderBuffer* buffer;
        TfToken name;
    };
    std::vector<Target> targets;
    unsigned int width = 0, height = 0;
    for (const HdRenderPassAovBinding& binding : renderPassState->GetAovBindings()) {
        auto* buffer = dynamic_cast<HdPhantasosRenderBuffer*>(binding.renderBuffer);
        if (!buffer) continue;
        targets.push_back({buffer, binding.aovName});
        if (width == 0 || binding.aovName == HdAovTokens->color) {
            width = buffer->GetWidth();
            height = buffer->GetHeight();
        }
    }
    if (targets.empty() || width == 0 || height == 0) {
        _converged = true;
        return;
    }

    HdPhantasosSceneStore* store = _renderDelegate->Store();
    Framing framing;
    framing.worldToView = renderPassState->GetWorldToViewMatrix();
    framing.projection = renderPassState->GetProjectionMatrix();
    framing.width = width;
    framing.height = height;
    framing.sceneRevision = store->Revision();
    framing.settingsVersion = _renderDelegate->GetRenderSettingsVersion();
    framing.renderTags = renderTags;
    std::sort(framing.renderTags.begin(), framing.renderTags.end());

    if (_hasFraming && framing == _framing) {
        // Nothing the frame depends on changed; the buffers still hold it.
        for (Target& target : targets) target.buffer->SetConverged(true);
        _converged = true;
        return;
    }

    // --- Flatten the store into subjects and draw items.
    std::uint64_t revision = 0;
    const auto meshes = store->Snapshot(&revision);
    framing.sceneRevision = revision;

    std::vector<phantasos::Subject> subjects;
    std::unordered_map<std::string, std::int32_t> subjectIndex;
    std::vector<phantasos::DrawItem> items;
    for (const auto& mesh : meshes) {
        if (!mesh->visible || !mesh->geometry || mesh->geometry->triangles.empty()) continue;
        if (!renderTags.empty()) {
            const TfToken tag = GetRenderIndex()->GetRenderTag(mesh->id);
            if (std::find(renderTags.begin(), renderTags.end(), tag) == renderTags.end()) continue;
        }
        auto [found, inserted] = subjectIndex.try_emplace(
            mesh->subject.key, static_cast<std::int32_t>(subjects.size()));
        if (inserted) {
            subjects.push_back(mesh->subject);
        } else {
            // The parts of one subject should agree; where one says more than
            // the first, its words are kept rather than lost.
            phantasos::Subject& subject = subjects[found->second];
            if (subject.description.empty()) subject.description = mesh->subject.description;
            if (subject.reference.empty()) subject.reference = mesh->subject.reference;
            subject.hasFront = subject.hasFront || mesh->subject.hasFront;
        }
        const HdPhantasosGeometry& geometry = *mesh->geometry;
        for (std::size_t i = 0; i < mesh->placements.size(); ++i) {
            phantasos::DrawItem item;
            item.points = geometry.points.data();
            item.pointCount = geometry.points.size() / 3;
            item.triangles = geometry.triangles.data();
            item.triangleCount = geometry.triangles.size() / 3;
            item.faces = geometry.faces.data();
            item.objectToWorld = mesh->placements[i];
            item.subject = found->second;
            item.primId = mesh->primId;
            item.instanceId = mesh->instanced ? static_cast<std::int32_t>(i) : -1;
            items.push_back(item);
        }
    }

    std::string tags;
    for (const TfToken& tag : renderTags) tags += (tags.empty() ? "" : ",") + tag.GetString();
    HdPhantasosTrace("execute %ux%u: %zu meshes in the store, %zu draw items, %zu subjects, "
                     "render tags [%s]",
                     width, height, meshes.size(), items.size(), subjects.size(), tags.c_str());

    // --- Draw.
    phantasos::Camera camera;
    camera.worldToView = ToMatrix(framing.worldToView);
    camera.projection = ToMatrix(framing.projection);

    phantasos::ProxyStyle style;
    style.outlineWidth = _renderDelegate->GetRenderSetting<float>(
        HdPhantasosTokens->outlineWidth, style.outlineWidth);
    // Two switches rather than one three-way setting: a host's settings panel
    // shows a bool as a checkbox and a token as bare text. Clay wins when both
    // are on; it is shaded by definition.
    if (_renderDelegate->GetRenderSetting<bool>(HdPhantasosTokens->proxyClay, false)) {
        style.look = phantasos::Look::Clay;
    } else if (_renderDelegate->GetRenderSetting<bool>(HdPhantasosTokens->proxyShading, false)) {
        style.look = phantasos::Look::Shaded;
    }
    style.checkerSize = _renderDelegate->GetRenderSetting<float>(
        HdPhantasosTokens->checkerSize, style.checkerSize);

    phantasos::RasterStats stats;
    phantasos::Rasterize(items, subjects, camera, style, static_cast<int>(width),
                         static_cast<int>(height), &_frame, &stats);

    HdPhantasosTrace("drew %zu triangles (%zu behind the camera) in %.1f ms",
                     stats.triangles, stats.culledTriangles, stats.milliseconds);

    for (Target& target : targets) {
        HdPhantasosRenderBuffer* buffer = target.buffer;
        if (buffer->GetWidth() != width || buffer->GetHeight() != height) {
            // A buffer of another size would need its own frame; it is left
            // as it was, and said once.
            static bool warned = false;
            if (!warned) {
                warned = true;
                TF_WARN("hdPhantasos: AOV '%s' is %ux%u, not %ux%u like the others; "
                        "it is not drawn",
                        target.name.GetText(), buffer->GetWidth(), buffer->GetHeight(),
                        width, height);
            }
        } else if (target.name == HdAovTokens->color) {
            buffer->WriteFloats(_frame.color, 4, true);
        } else if (target.name == HdAovTokens->depth) {
            buffer->WriteFloats(_frame.depth, 1, false);
        } else if (target.name == HdAovTokens->normal) {
            buffer->WriteFloats(_frame.normal, 3, false);
        } else if (target.name == HdAovTokens->primId) {
            buffer->WriteInts(_frame.primId);
        } else if (target.name == HdAovTokens->instanceId) {
            buffer->WriteInts(_frame.instanceId);
        } else if (target.name == HdAovTokens->elementId) {
            buffer->WriteInts(_frame.elementId);
        } else if (target.name == HdPhantasosTokens->subjectId) {
            buffer->WriteInts(_frame.subject);
        }
        buffer->SetConverged(true);
    }

    // --- What the frame shows of each subject.
    const auto views = phantasos::MeasureSubjects(items, subjects, camera, _frame);
    const std::string manifestDirectory = TfGetenv("HDPHANTASOS_MANIFEST");
    if (!manifestDirectory.empty()) {
        WriteManifest(manifestDirectory,
                      phantasos::ManifestJson(subjects, views, camera, static_cast<int>(width),
                                              static_cast<int>(height)),
                      _frame);
    }

    int visibleSubjects = 0;
    for (const auto& view : views) visibleSubjects += view.pixels > 0 ? 1 : 0;
    VtDictionary record;
    record["phantasos:triangles"] = VtValue(static_cast<int>(stats.triangles));
    record["phantasos:milliseconds"] = VtValue(stats.milliseconds);
    record["phantasos:subjects"] = VtValue(static_cast<int>(subjects.size()));
    record["phantasos:visibleSubjects"] = VtValue(visibleSubjects);
    _renderDelegate->RecordFrame(record);

    _framing = framing;
    _hasFraming = true;
    _converged = true;
}

PXR_NAMESPACE_CLOSE_SCOPE
