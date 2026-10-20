#include "render_buffer.h"

#include "pxr/base/gf/half.h"
#include "pxr/base/gf/vec3i.h"
#include "pxr/base/tf/diagnostic.h"

#include <algorithm>
#include <cmath>
#include <cstring>

PXR_NAMESPACE_OPEN_SCOPE

HdPhantasosRenderBuffer::HdPhantasosRenderBuffer(const SdfPath& id) : HdRenderBuffer(id) {}

HdPhantasosRenderBuffer::~HdPhantasosRenderBuffer() = default;

bool HdPhantasosRenderBuffer::Allocate(const GfVec3i& dimensions, HdFormat format,
                                       bool /*multiSampled*/)
{
    _Deallocate();
    if (dimensions[2] != 1) {
        TF_WARN("hdPhantasos: <%s> asked for a %d-deep render buffer; only 2D "
                "buffers are supported",
                GetId().GetText(), dimensions[2]);
        return false;
    }
    if (format == HdFormatInvalid) return false;
    _width = static_cast<unsigned int>(std::max(0, dimensions[0]));
    _height = static_cast<unsigned int>(std::max(0, dimensions[1]));
    _format = format;
    _data.assign(static_cast<std::size_t>(_width) * _height * HdDataSizeOfFormat(format), 0);
    return true;
}

void HdPhantasosRenderBuffer::_Deallocate()
{
    _width = _height = 0;
    _format = HdFormatInvalid;
    _data.clear();
    _data.shrink_to_fit();
    _converged = false;
    _mappers.store(0);
}

void HdPhantasosRenderBuffer::WriteFloats(const std::vector<float>& values, std::size_t stride,
                                          bool colour)
{
    const std::size_t pixels = static_cast<std::size_t>(_width) * _height;
    if (pixels == 0 || stride == 0 || values.size() < pixels * stride) return;
    const std::size_t channels = std::min<std::size_t>(stride, HdGetComponentCount(_format));
    const HdFormat component = HdGetComponentFormat(_format);
    const std::size_t pixelSize = HdDataSizeOfFormat(_format);

    for (std::size_t i = 0; i < pixels; ++i) {
        std::uint8_t* destination = _data.data() + i * pixelSize;
        for (std::size_t c = 0; c < channels; ++c) {
            const float value = values[i * stride + c];
            switch (component) {
                case HdFormatFloat32:
                    std::memcpy(destination + c * sizeof(float), &value, sizeof(float));
                    break;
                case HdFormatFloat16: {
                    const GfHalf half(value);
                    std::memcpy(destination + c * sizeof(GfHalf), &half, sizeof(GfHalf));
                    break;
                }
                case HdFormatUNorm8: {
                    float encoded = std::clamp(value, 0.0f, 1.0f);
                    if (colour && c < 3) {
                        encoded = encoded <= 0.0031308f
                                      ? encoded * 12.92f
                                      : 1.055f * std::pow(encoded, 1.0f / 2.4f) - 0.055f;
                    }
                    destination[c] = static_cast<std::uint8_t>(encoded * 255.0f + 0.5f);
                    break;
                }
                default:
                    // A format this does not know how to fill is left at its
                    // clear value rather than filled with reinterpreted bytes.
                    return;
            }
        }
    }
}

void HdPhantasosRenderBuffer::WriteInts(const std::vector<std::int32_t>& values)
{
    const std::size_t pixels = static_cast<std::size_t>(_width) * _height;
    if (pixels == 0 || values.size() < pixels || HdGetComponentFormat(_format) != HdFormatInt32) {
        return;
    }
    const std::size_t pixelSize = HdDataSizeOfFormat(_format);
    for (std::size_t i = 0; i < pixels; ++i) {
        std::memcpy(_data.data() + i * pixelSize, &values[i], sizeof(std::int32_t));
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
