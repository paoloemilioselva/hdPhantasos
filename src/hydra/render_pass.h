#pragma once

#include "api.h"

#include "hdphantasos/core/raster.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/imaging/hd/renderPass.h"

#include <cstdint>

PXR_NAMESPACE_OPEN_SCOPE

class HdPhantasosRenderDelegate;

/// Rasterizes the scene store into the bound AOVs.
///
/// One execute draws one finished frame, so every pass is converged when it
/// returns. A frame is drawn again only when something it depends on changed:
/// the camera, the AOV size, the scene, the settings or the render tags.
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
};

PXR_NAMESPACE_CLOSE_SCOPE
