#include "instancer.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/quatd.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/quath.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

HdPhantasosInstancer::HdPhantasosInstancer(HdSceneDelegate* delegate, const SdfPath& id)
    : HdInstancer(delegate, id)
{
}

HdPhantasosInstancer::~HdPhantasosInstancer() = default;

void HdPhantasosInstancer::Sync(HdSceneDelegate* sceneDelegate,
                                HdRenderParam* /*renderParam*/, HdDirtyBits* dirtyBits)
{
    // The parent chain first: a nested instancer composes its parent's.
    _UpdateInstancer(sceneDelegate, dirtyBits);

    const SdfPath& id = GetId();
    if (!HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, id)) return;
    for (const HdPrimvarDescriptor& descriptor :
         sceneDelegate->GetPrimvarDescriptors(id, HdInterpolationInstance)) {
        if (!HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, descriptor.name)) continue;
        const VtValue value = sceneDelegate->Get(id, descriptor.name);
        if (!value.IsEmpty()) _primvars[descriptor.name] = value;
    }
}

namespace {

/// A rotation primvar in either authored form: quaternions, or the vec4s
/// older assets carry. Refusing one silently drops every orientation.
bool RotationAt(const VtValue& value, std::size_t index, GfQuatd* out)
{
    if (value.IsHolding<VtQuathArray>()) {
        const auto& rotations = value.UncheckedGet<VtQuathArray>();
        if (index >= rotations.size()) return false;
        *out = GfQuatd(rotations[index].GetReal(), GfVec3d(rotations[index].GetImaginary()));
        return true;
    }
    if (value.IsHolding<VtQuatfArray>()) {
        const auto& rotations = value.UncheckedGet<VtQuatfArray>();
        if (index >= rotations.size()) return false;
        *out = GfQuatd(rotations[index].GetReal(), GfVec3d(rotations[index].GetImaginary()));
        return true;
    }
    if (value.IsHolding<VtQuatdArray>()) {
        const auto& rotations = value.UncheckedGet<VtQuatdArray>();
        if (index >= rotations.size()) return false;
        *out = rotations[index];
        return true;
    }
    if (value.IsHolding<VtVec4fArray>()) {
        const auto& rotations = value.UncheckedGet<VtVec4fArray>();
        if (index >= rotations.size()) return false;
        const GfVec4f& r = rotations[index];
        *out = GfQuatd(r[0], GfVec3d(r[1], r[2], r[3]));
        return true;
    }
    return false;
}

}  // namespace

VtMatrix4dArray HdPhantasosInstancer::ComputeInstanceTransforms(const SdfPath& prototypeId)
{
    HdSceneDelegate* delegate = GetDelegate();
    const SdfPath& id = GetId();
    const GfMatrix4d instancerTransform = delegate->GetInstancerTransform(id);
    const VtIntArray indices = delegate->GetInstanceIndices(id, prototypeId);

    const auto find = [&](const TfToken& name) -> const VtValue* {
        const auto it = _primvars.find(name);
        return it == _primvars.end() ? nullptr : &it->second;
    };
    const VtValue* translate = find(HdInstancerTokens->instanceTranslations);
    const VtValue* rotate = find(HdInstancerTokens->instanceRotations);
    const VtValue* scale = find(HdInstancerTokens->instanceScales);
    const VtValue* matrices = find(HdInstancerTokens->instanceTransforms);

    VtMatrix4dArray transforms;
    transforms.reserve(indices.size());
    for (const int index : indices) {
        const auto at = static_cast<std::size_t>(index);
        GfMatrix4d transform = instancerTransform;
        if (translate && translate->IsHolding<VtVec3fArray>()) {
            const auto& values = translate->UncheckedGet<VtVec3fArray>();
            if (at < values.size()) {
                GfMatrix4d step(1.0);
                step.SetTranslate(GfVec3d(values[at]));
                transform = step * transform;
            }
        }
        if (rotate) {
            GfQuatd rotation;
            if (RotationAt(*rotate, at, &rotation)) {
                GfMatrix4d step(1.0);
                step.SetRotate(rotation);
                transform = step * transform;
            }
        }
        if (scale && scale->IsHolding<VtVec3fArray>()) {
            const auto& values = scale->UncheckedGet<VtVec3fArray>();
            if (at < values.size()) {
                GfMatrix4d step(1.0);
                step.SetScale(GfVec3d(values[at]));
                transform = step * transform;
            }
        }
        if (matrices && matrices->IsHolding<VtMatrix4dArray>()) {
            const auto& values = matrices->UncheckedGet<VtMatrix4dArray>();
            if (at < values.size()) transform = values[at] * transform;
        }
        transforms.push_back(transform);
    }

    if (GetParentId().IsEmpty()) return transforms;
    auto* parent = dynamic_cast<HdPhantasosInstancer*>(
        delegate->GetRenderIndex().GetInstancer(GetParentId()));
    if (!parent) return transforms;

    const VtMatrix4dArray parentTransforms = parent->ComputeInstanceTransforms(id);
    VtMatrix4dArray composed;
    composed.reserve(parentTransforms.size() * transforms.size());
    for (const GfMatrix4d& outer : parentTransforms) {
        for (const GfMatrix4d& inner : transforms) composed.push_back(inner * outer);
    }
    return composed;
}

PXR_NAMESPACE_CLOSE_SCOPE
