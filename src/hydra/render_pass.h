#pragma once

#include "api.h"

#include "hdphantasos/core/raster.h"

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

    // Generation: the request the current frame is waiting for (0: none).
    std::uint64_t _nextKey = 0;
    std::uint64_t _awaitingKey = 0;
    std::chrono::steady_clock::time_point _submitted;
    std::string _reportedError;
};

PXR_NAMESPACE_CLOSE_SCOPE
