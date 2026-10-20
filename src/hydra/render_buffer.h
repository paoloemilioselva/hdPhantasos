#pragma once

#include "pxr/imaging/hd/renderBuffer.h"

#include <atomic>
#include <cstdint>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// A CPU AOV. The rasterizer runs on the CPU, so this is where its output
/// lives; nothing is ever on a device.
class HdPhantasosRenderBuffer final : public HdRenderBuffer {
  public:
    explicit HdPhantasosRenderBuffer(const SdfPath& id);
    ~HdPhantasosRenderBuffer() override;

    bool Allocate(const GfVec3i& dimensions, HdFormat format, bool multiSampled) override;

    unsigned int GetWidth() const override { return _width; }
    unsigned int GetHeight() const override { return _height; }
    unsigned int GetDepth() const override { return 1; }
    HdFormat GetFormat() const override { return _format; }
    bool IsMultiSampled() const override { return false; }

    void* Map() override
    {
        ++_mappers;
        return _data.data();
    }
    void Unmap() override { --_mappers; }
    bool IsMapped() const override { return _mappers.load() != 0; }

    void Resolve() override {}
    bool IsConverged() const override { return _converged; }
    void SetConverged(bool converged) { _converged = converged; }

    /// `stride` floats a pixel, into however many channels the buffer has.
    /// A UNorm8 buffer gets the sRGB curve when `colour` is set, since the
    /// values are linear and an 8-bit buffer is a display image.
    void WriteFloats(const std::vector<float>& values, std::size_t stride, bool colour);

    /// One integer a pixel: the id AOVs.
    void WriteInts(const std::vector<std::int32_t>& values);

  protected:
    void _Deallocate() override;

  private:
    unsigned int _width = 0;
    unsigned int _height = 0;
    HdFormat _format = HdFormatInvalid;
    std::vector<std::uint8_t> _data;
    std::atomic<int> _mappers{0};
    bool _converged = false;
};

PXR_NAMESPACE_CLOSE_SCOPE
