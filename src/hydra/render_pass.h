#pragma once

#include "api.h"

#include "hdphantasos/core/raster.h"
#include "worker_link.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/imaging/hd/renderPass.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

class HdPhantasosRenderDelegate;

class HdPhantasosRenderBuffer;

/// Rasterizes the scene store into the bound AOVs.
///
/// A frame is drawn again only when something it depends on changed: the
/// camera, the AOV size, the scene, the settings or the render tags. Without
/// generation, one execute draws one finished frame. With it, the proxy is
/// shown at once and handed to the model worker; the pass stays unconverged,
/// so the host keeps calling, until the worker's picture replaces the proxy
/// in the colour AOV. A newer view replaces a request still waiting.
///
/// A streaming model (Matrix-Game) paints a few frames behind: every picture
/// it sends is shown, and while the newest shows an older view than the
/// current one, the current proxy is sent again so the stream catches up.
class HDPHANTASOS_API HdPhantasosRenderPass final : public HdRenderPass {
  public:
    HdPhantasosRenderPass(HdRenderIndex* index, const HdRprimCollection& collection,
                          HdPhantasosRenderDelegate* renderDelegate);
    ~HdPhantasosRenderPass() override;

    bool IsConverged() const override { return _converged; }

  protected:
    void _Execute(const HdRenderPassStateSharedPtr& renderPassState,
                  const TfTokenVector& renderTags) override;

  private:
    struct Target {
        HdPhantasosRenderBuffer* buffer;
        TfToken name;
    };

    /// Checks for the worker's picture of the current frame, and shows it.
    void _PollGeneration(const std::vector<Target>& targets);
    void _SetConverged(const std::vector<Target>& targets, bool converged);

    struct Framing {
        GfMatrix4d worldToView;
        GfMatrix4d projection;
        unsigned int width = 0;
        unsigned int height = 0;
        std::uint64_t sceneRevision = 0;
        unsigned int settingsVersion = 0;
        TfTokenVector renderTags;
        bool operator==(const Framing& other) const;
    };

    HdPhantasosRenderDelegate* _renderDelegate;
    Framing _framing;
    bool _hasFraming = false;
    bool _converged = false;
    phantasos::Frame _frame;

    // Generation: the request the current frame is waiting for (0: none),
    // and the first request made for the current view: a streamed picture
    // of that request or a later one shows this view.
    std::uint64_t _nextKey = 0;
    std::uint64_t _awaitingKey = 0;
    std::uint64_t _viewKey = 0;
    HdPhantasosWorkerLink::Request _lastRequest;  // sent again to feed a stream
    // The last streamed picture, as the colour AOV holds it: kept on screen
    // while the camera moves, rather than flashing back to the proxy.
    std::vector<float> _streamColor;
    unsigned int _streamWidth = 0, _streamHeight = 0;
    std::string _streamModel;
    std::chrono::steady_clock::time_point _submitted;
    std::string _reportedError;
};

PXR_NAMESPACE_CLOSE_SCOPE
