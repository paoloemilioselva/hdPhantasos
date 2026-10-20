#include "render_pass.h"

#include "render_buffer.h"
#include "render_delegate.h"
#include "tokens.h"
#include "trace.h"
#include "worker_link.h"

#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/tf/getenv.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/imaging/hd/renderPassState.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hio/image.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <optional>
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

bool MakeDirectory(const std::string& directory)
{
    if (TfIsDir(directory) || TfMakeDirs(directory, -1, true)) return true;
    TF_WARN("hdPhantasos: cannot create the manifest directory '%s'", directory.c_str());
    return false;
}

void WritePng(const std::string& path, const std::uint8_t* rgb, int width, int height)
{
    HioImageSharedPtr image = HioImage::OpenForWriting(path);
    if (!image) return;
    HioImage::StorageSpec storage;
    storage.width = width;
    storage.height = height;
    storage.format = HioFormatUNorm8Vec3srgb;
    storage.flipped = false;
    storage.data = const_cast<std::uint8_t*>(rgb);
    if (!image->Write(storage)) TF_WARN("hdPhantasos: could not write '%s'", path.c_str());
}

/// Writes what the frame shows, for looking at and for tests: manifest.json
/// (the subjects, their descriptions and where they are) and proxy.png (the
/// guide image), into HDPHANTASOS_MANIFEST when it names a directory. The
/// generated picture and its prompt follow when they arrive. Each frame
/// overwrites the last.
void WriteManifest(const std::string& directory, const std::string& json,
                   const phantasos::Frame& frame)
{
    if (!MakeDirectory(directory)) return;
    std::ofstream(TfStringCatPaths(directory, "manifest.json"), std::ios::binary) << json;
    const std::vector<std::uint8_t> rgb = phantasos::ProxyRgb8(frame);
    WritePng(TfStringCatPaths(directory, "proxy.png"), rgb.data(), frame.width, frame.height);
}

double GenerateTimeoutSeconds()
{
    const std::string text = TfGetenv("HDPHANTASOS_GENERATE_TIMEOUT");
    return text.empty() ? 3600.0 : std::atof(text.c_str());
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

void HdPhantasosRenderPass::_SetConverged(const std::vector<Target>& targets, bool converged)
{
    for (const Target& target : targets) target.buffer->SetConverged(converged);
    _converged = converged;
}

void HdPhantasosRenderPass::_Execute(const HdRenderPassStateSharedPtr& renderPassState,
                                     const TfTokenVector& renderTags)
{
    if (!_renderDelegate || !renderPassState) return;

    // --- Which AOVs, and at what size: the colour buffer's, or the first.
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
        // Nothing the frame depends on changed; the buffers still hold it,
        // unless a picture of it is still on its way.
        if (_awaitingKey != 0) {
            _PollGeneration(targets);
        } else {
            _SetConverged(targets, true);
        }
        return;
    }

    // --- Flatten the store into subjects and draw items.
    std::uint64_t revision = 0;
    const auto meshes = store->Snapshot(&revision);
    framing.sceneRevision = revision;

    std::vector<phantasos::Subject> subjects;
    std::unordered_map<std::string, std::int32_t> subjectIndex;
    std::vector<phantasos::DrawItem> items;
    std::string shot;
    for (const auto& mesh : meshes) {
        if (!mesh->visible || !mesh->geometry || mesh->geometry->triangles.empty()) continue;
        if (!renderTags.empty()) {
            const TfToken tag = GetRenderIndex()->GetRenderTag(mesh->id);
            if (std::find(renderTags.begin(), renderTags.end(), tag) == renderTags.end()) continue;
        }
        if (shot.empty()) shot = mesh->shot;
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
    }

    // --- What the frame shows of each subject.
    const auto views = phantasos::MeasureSubjects(items, subjects, camera, _frame);
    const std::string manifest = phantasos::ManifestJson(
        subjects, views, camera, static_cast<int>(width), static_cast<int>(height), shot);
    const std::string manifestDirectory = TfGetenv("HDPHANTASOS_MANIFEST");
    if (!manifestDirectory.empty()) WriteManifest(manifestDirectory, manifest, _frame);

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

    // --- Generation: hand the proxy and the manifest to the worker, and keep
    // the host calling until its picture is here.
    if (!_renderDelegate->GetRenderSetting<bool>(HdPhantasosTokens->generate, false)) {
        _awaitingKey = 0;
        _streamColor.clear();
        _SetConverged(targets, true);
        return;
    }
    HdPhantasosWorkerLink::Request request;
    request.key = ++_nextKey;
    request.width = static_cast<int>(width);
    request.height = static_cast<int>(height);
    request.rgb = phantasos::ProxyRgb8(_frame);
    // One line: the manifest's newlines are all between tokens.
    request.manifest = manifest;
    std::replace(request.manifest.begin(), request.manifest.end(), '\n', ' ');
    const std::string model =
        _renderDelegate->GetRenderSetting<std::string>(HdPhantasosTokens->model, "klein4b");
    if (model != _streamModel) _streamColor.clear();
    _streamModel = model;
    if (!_streamColor.empty() && _streamWidth == width && _streamHeight == height) {
        for (const Target& target : targets) {
            if (target.name == HdAovTokens->color) target.buffer->WriteFloats(_streamColor, 4, true);
        }
    }
    const std::string extraStyle =
        _renderDelegate->GetRenderSetting<std::string>(HdPhantasosTokens->style, "");
    request.settings =
        "{\"model\": " + HdPhantasosJsonQuote(model) +
        ", \"size\": " + std::to_string(_renderDelegate->GetRenderSetting<int>(HdPhantasosTokens->generateSize, 512)) +
        ", \"seed\": " + std::to_string(_renderDelegate->GetRenderSetting<int>(HdPhantasosTokens->seed, 0)) +
        ", \"steps\": 4, \"look\": " + HdPhantasosJsonQuote(phantasos::LookName(style.look)) +
        ", \"style\": " + HdPhantasosJsonQuote(extraStyle) +
        ", \"firstFrame\": " + HdPhantasosJsonQuote(_renderDelegate->GetRenderSetting<std::string>(
                                     HdPhantasosTokens->firstFrame, "model")) + "}";
    _lastRequest = request;
    _renderDelegate->WorkerLink()->Submit(std::move(request));
    _awaitingKey = _nextKey;
    _viewKey = _nextKey;
    _submitted = std::chrono::steady_clock::now();
    _SetConverged(targets, false);
    // A stream may have a picture ready already: show it now, not after the
    // camera stops. (A still for an earlier view is dropped there.)
    _PollGeneration(targets);
}

void HdPhantasosRenderPass::_PollGeneration(const std::vector<Target>& targets)
{
    HdPhantasosWorkerLink* link = _renderDelegate->WorkerLink();
    std::optional<HdPhantasosWorkerLink::Result> result = link->Take();
    if (result && !result->stream && result->key != _awaitingKey) {
        // A still for a view the camera has left: not worth showing.
        result.reset();
    }
    if (!result) {
        // Bounded, so a render-until-converged host such as usdrecord cannot
        // wait forever on a worker that never answers. The first frame of a
        // session includes setting up and loading the model.
        const double waited =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - _submitted).count();
        if (waited > GenerateTimeoutSeconds()) {
            TF_WARN("hdPhantasos: no picture from the model worker after %.0f s (%s); "
                    "showing the proxy",
                    waited, link->Status().c_str());
            _awaitingKey = 0;
            _SetConverged(targets, true);
            return;
        }
        _SetConverged(targets, false);
        return;
    }
    _awaitingKey = 0;
    // A stream that has not reached the current view yet is fed the same
    // proxy again; its picture is shown meanwhile.
    const bool behind = result->stream && result->error.empty() && result->source < _viewKey;

    VtDictionary record = _renderDelegate->GetRenderStats();
    record["phantasos:worker"] = VtValue(link->Status());
    if (!result->error.empty()) {
        // Said once per distinct reason: a host calls every frame, and the
        // same sentence sixty times a second buries everything else.
        if (result->error != _reportedError) {
            _reportedError = result->error;
            TF_WARN("hdPhantasos: no picture from the model worker: %s", result->error.c_str());
        }
        _renderDelegate->RecordFrame(record);
        _SetConverged(targets, true);
        return;
    }
    _reportedError.clear();

    // --- The worker's picture into the colour AOV: resampled bilinearly to
    // the AOV's size, sRGB to linear, rows flipped to bottom-up.
    const unsigned int width = _framing.width, height = _framing.height;
    static const std::vector<float> toLinear = [] {
        std::vector<float> table(256);
        for (int i = 0; i < 256; ++i) {
            const float c = i / 255.0f;
            table[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return table;
    }();
    std::vector<float> color(static_cast<std::size_t>(width) * height * 4, 1.0f);
    const int sw = result->width, sh = result->height;
    const std::uint8_t* source = result->rgb.data();
    for (unsigned int row = 0; row < height; ++row) {
        // A bottom-up AOV row, read from a top-down source, by pixel centres.
        const float sy = std::clamp((height - 1 - row + 0.5f) * sh / height - 0.5f, 0.0f, sh - 1.0f);
        const int y0 = static_cast<int>(sy), y1 = std::min(y0 + 1, sh - 1);
        const float fy = sy - y0;
        for (unsigned int x = 0; x < width; ++x) {
            const float sx = std::clamp((x + 0.5f) * sw / width - 0.5f, 0.0f, sw - 1.0f);
            const int x0 = static_cast<int>(sx), x1 = std::min(x0 + 1, sw - 1);
            const float fx = sx - x0;
            for (int c = 0; c < 3; ++c) {
                const auto at = [&](int px, int py) { return toLinear[source[(py * sw + px) * 3 + c]]; };
                const float top = at(x0, y0) * (1 - fx) + at(x1, y0) * fx;
                const float bottom = at(x0, y1) * (1 - fx) + at(x1, y1) * fx;
                color[(static_cast<std::size_t>(row) * width + x) * 4 + c] = top * (1 - fy) + bottom * fy;
            }
        }
    }
    for (const Target& target : targets) {
        if (target.name == HdAovTokens->color && target.buffer->GetWidth() == width &&
            target.buffer->GetHeight() == height) {
            target.buffer->WriteFloats(color, 4, true);
        }
    }
    if (result->stream) {
        _streamColor = std::move(color);
        _streamWidth = width;
        _streamHeight = height;
    } else {
        _streamColor.clear();
    }

    const std::string manifestDirectory = TfGetenv("HDPHANTASOS_MANIFEST");
    if (!manifestDirectory.empty() && MakeDirectory(manifestDirectory)) {
        std::ofstream(TfStringCatPaths(manifestDirectory, "prompt.txt"), std::ios::binary) << result->prompt;
        WritePng(TfStringCatPaths(manifestDirectory, "generated.png"), source, sw, sh);
    }

    HdPhantasosTrace("generated %dx%d in %.0f ms", sw, sh, result->milliseconds);
    record["phantasos:generateMilliseconds"] = VtValue(result->milliseconds);
    record["phantasos:prompt"] = VtValue(result->prompt);
    _renderDelegate->RecordFrame(record);
    if (behind) {
        HdPhantasosWorkerLink::Request again = _lastRequest;
        again.key = ++_nextKey;
        link->Submit(std::move(again));
        _awaitingKey = _nextKey;
        _submitted = std::chrono::steady_clock::now();
        _SetConverged(targets, false);
        return;
    }
    _SetConverged(targets, true);
}

PXR_NAMESPACE_CLOSE_SCOPE
