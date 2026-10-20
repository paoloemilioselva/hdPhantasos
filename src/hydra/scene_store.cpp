#include "scene_store.h"

PXR_NAMESPACE_OPEN_SCOPE

void HdPhantasosSceneStore::Set(const SdfPath& id,
                                std::shared_ptr<const HdPhantasosMeshRecord> record)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _meshes[id] = std::move(record);
    ++_revision;
}

void HdPhantasosSceneStore::Remove(const SdfPath& id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_meshes.erase(id) != 0) ++_revision;
}

std::vector<std::shared_ptr<const HdPhantasosMeshRecord>> HdPhantasosSceneStore::Snapshot(
    std::uint64_t* revision) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<std::shared_ptr<const HdPhantasosMeshRecord>> meshes;
    meshes.reserve(_meshes.size());
    for (const auto& [id, record] : _meshes) meshes.push_back(record);
    if (revision) *revision = _revision;
    return meshes;
}

std::uint64_t HdPhantasosSceneStore::Revision() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _revision;
}

PXR_NAMESPACE_CLOSE_SCOPE
