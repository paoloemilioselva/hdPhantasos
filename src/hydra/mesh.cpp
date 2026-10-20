#include "mesh.h"

#include "instancer.h"
#include "render_param.h"
#include "tokens.h"
#include "trace.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/vt/array.h"
#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {

phantasos::Matrix4 ToMatrix(const GfMatrix4d& m)
{
    // Both are row-major and transform row vectors: a straight copy.
    phantasos::Matrix4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) r.m[i][j] = m[i][j];
    return r;
}

/// A constant primvar's value as text, whichever string-like type it was
/// authored as. A one-element array counts, as some exporters write those.
bool AsString(const VtValue& value, std::string* out)
{
    if (value.IsHolding<std::string>()) {
        *out = value.UncheckedGet<std::string>();
        return true;
    }
    if (value.IsHolding<TfToken>()) {
        *out = value.UncheckedGet<TfToken>().GetString();
        return true;
    }
    if (value.IsHolding<VtStringArray>() && !value.UncheckedGet<VtStringArray>().empty()) {
        *out = value.UncheckedGet<VtStringArray>()[0];
        return true;
    }
    if (value.IsHolding<VtTokenArray>() && !value.UncheckedGet<VtTokenArray>().empty()) {
        *out = value.UncheckedGet<VtTokenArray>()[0].GetString();
        return true;
    }
    if (value.IsHolding<SdfAssetPath>()) {
        const SdfAssetPath& asset = value.UncheckedGet<SdfAssetPath>();
        *out = asset.GetResolvedPath().empty() ? asset.GetAssetPath() : asset.GetResolvedPath();
        return true;
    }
    return false;
}

bool AsBool(const VtValue& value, bool* out)
{
    if (value.IsHolding<bool>()) {
        *out = value.UncheckedGet<bool>();
        return true;
    }
    if (value.IsHolding<VtBoolArray>() && !value.UncheckedGet<VtBoolArray>().empty()) {
        *out = value.UncheckedGet<VtBoolArray>()[0];
        return true;
    }
    if (value.IsHolding<int>()) {
        *out = value.UncheckedGet<int>() != 0;
        return true;
    }
    return false;
}

bool AsColor(const VtValue& value, float out[3])
{
    GfVec3f color;
    if (value.IsHolding<GfVec3f>()) {
        color = value.UncheckedGet<GfVec3f>();
    } else if (value.IsHolding<VtVec3fArray>() && !value.UncheckedGet<VtVec3fArray>().empty()) {
        color = value.UncheckedGet<VtVec3fArray>()[0];
    } else {
        return false;
    }
    for (int c = 0; c < 3; ++c) out[c] = color[c];
    return true;
}

}  // namespace

HdPhantasosMesh::HdPhantasosMesh(const SdfPath& id) : HdMesh(id) {}

HdPhantasosMesh::~HdPhantasosMesh() = default;

HdDirtyBits HdPhantasosMesh::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::Clean | HdChangeTracker::InitRepr |
           HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTopology |
           HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility |
           HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyInstancer |
           HdChangeTracker::DirtyInstanceIndex | HdChangeTracker::DirtyRenderTag |
           HdChangeTracker::DirtyPrimID;
}

HdDirtyBits HdPhantasosMesh::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void HdPhantasosMesh::_InitRepr(const TfToken& /*reprToken*/, HdDirtyBits* /*dirtyBits*/) {}

void HdPhantasosMesh::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam,
                           HdDirtyBits* dirtyBits, const TfToken& /*reprToken*/)
{
    auto* param = static_cast<HdPhantasosRenderParam*>(renderParam);
    const SdfPath& id = GetId();
    const HdDirtyBits bits = *dirtyBits;

    // Not filled in by Hydra on its own: the instancer id, and its chain
    // synced, before any placement can be computed.
    _UpdateInstancer(sceneDelegate, dirtyBits);
    HdInstancer::_SyncInstancerAndParents(sceneDelegate->GetRenderIndex(), GetInstancerId());

    if (!_record.geometry || (bits & (HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTopology))) {
        auto geometry = std::make_shared<HdPhantasosGeometry>();
        const VtValue pointsValue = sceneDelegate->Get(id, HdTokens->points);
        if (pointsValue.IsHolding<VtVec3fArray>()) {
            const VtVec3fArray& points = pointsValue.UncheckedGet<VtVec3fArray>();
            geometry->points.resize(points.size() * 3);
            for (std::size_t i = 0; i < points.size(); ++i) {
                for (int c = 0; c < 3; ++c) geometry->points[i * 3 + c] = points[i][c];
            }
        }
        // HdMeshUtil owns the n-gon fans and the holes.
        const HdMeshTopology topology = GetMeshTopology(sceneDelegate);
        HdMeshUtil meshUtil(&topology, id);
        VtVec3iArray triangles;
        VtIntArray primitiveParams;
        meshUtil.ComputeTriangleIndices(&triangles, &primitiveParams);
        geometry->triangles.resize(triangles.size() * 3);
        geometry->faces.resize(triangles.size());
        for (std::size_t t = 0; t < triangles.size(); ++t) {
            for (int c = 0; c < 3; ++c) geometry->triangles[t * 3 + c] = triangles[t][c];
            geometry->faces[t] = t < primitiveParams.size()
                                     ? HdMeshUtil::DecodeFaceIndexFromCoarseFaceParam(primitiveParams[t])
                                     : static_cast<int>(t);
        }
        _record.geometry = std::move(geometry);
    }

    if (_record.placements.empty() ||
        (bits & (HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyInstancer |
                 HdChangeTracker::DirtyInstanceIndex))) {
        const GfMatrix4d meshTransform = sceneDelegate->GetTransform(id);
        _record.placements.clear();
        _record.instanced = !GetInstancerId().IsEmpty();
        if (!_record.instanced) {
            _record.placements.push_back(ToMatrix(meshTransform));
        } else if (auto* instancer = dynamic_cast<HdPhantasosInstancer*>(
                       sceneDelegate->GetRenderIndex().GetInstancer(GetInstancerId()))) {
            for (const GfMatrix4d& placement : instancer->ComputeInstanceTransforms(id)) {
                _record.placements.push_back(ToMatrix(meshTransform * placement));
            }
        }
    }

    if (bits & HdChangeTracker::DirtyVisibility) {
        _record.visible = sceneDelegate->GetVisible(id);
    }
    _record.id = id;
    _record.primId = GetPrimId();

    if (HdChangeTracker::IsAnyPrimvarDirty(bits, id) || _record.subject.key.empty()) {
        // Constant primvars are inherited down the namespace, so one authored
        // on a group's Xform reaches every mesh under it: that is how the
        // parts of a chest become one subject.
        phantasos::Subject subject;
        subject.key = id.GetString();
        _record.shot.clear();
        for (const HdPrimvarDescriptor& descriptor :
             GetPrimvarDescriptors(sceneDelegate, HdInterpolationConstant)) {
            const TfToken& name = descriptor.name;
            if (name == HdPhantasosTokens->subject) {
                std::string key;
                if (AsString(GetPrimvar(sceneDelegate, name), &key) && !key.empty()) {
                    subject.key = key;
                }
            } else if (name == HdPhantasosTokens->description) {
                AsString(GetPrimvar(sceneDelegate, name), &subject.description);
            } else if (name == HdPhantasosTokens->reference) {
                AsString(GetPrimvar(sceneDelegate, name), &subject.reference);
            } else if (name == HdPhantasosTokens->hasFront) {
                AsBool(GetPrimvar(sceneDelegate, name), &subject.hasFront);
            } else if (name == HdPhantasosTokens->proxyColor) {
                subject.hasColor = AsColor(GetPrimvar(sceneDelegate, name), subject.color);
            } else if (name == HdPhantasosTokens->shot) {
                AsString(GetPrimvar(sceneDelegate, name), &_record.shot);
            } else if (name == HdPhantasosTokens->role) {
                std::string role;
                AsString(GetPrimvar(sceneDelegate, name), &role);
                if (role == HdPhantasosTokens->ground.GetString()) {
                    subject.role = phantasos::Role::Ground;
                } else if (role == HdPhantasosTokens->backdrop.GetString()) {
                    subject.role = phantasos::Role::Backdrop;
                } else if (role == HdPhantasosTokens->hero.GetString()) {
                    subject.role = phantasos::Role::Hero;
                }
            }
        }
        _record.subject = std::move(subject);
    }

    HdPhantasosTrace("sync %s: %zu triangles, %zu placements, %s, subject '%s'",
                     id.GetText(), _record.geometry->triangles.size() / 3,
                     _record.placements.size(),
                     _record.visible ? "visible" : "hidden", _record.subject.key.c_str());
    param->Store()->Set(id, std::make_shared<const HdPhantasosMeshRecord>(_record));
    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

void HdPhantasosMesh::Finalize(HdRenderParam* renderParam)
{
    static_cast<HdPhantasosRenderParam*>(renderParam)->Store()->Remove(GetId());
}

PXR_NAMESPACE_CLOSE_SCOPE
