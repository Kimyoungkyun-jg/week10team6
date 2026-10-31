#include "EnginePCH.h"
#include "Render/SkeletalMesh.h"
#include "Render/Renderer.h"
#include "Asset/AssetManager.h"
#include "Engine/PrimitiveSceneProxy.h"
#include <algorithm>
#include <cmath>
#include <limits>

USkeletalMesh::~USkeletalMesh()
{
    for (FPrimitiveSceneProxy* Proxy : RenderProxies)
    {
        Proxy->OnMeshDestroyed();
    }
    VertexBuffer = nullptr;
    IndexBuffer = nullptr;
}

UMaterial* USkeletalMesh::GetMaterial(uint32 SlotIndex) const
{
    if (SlotIndex < Materials.size() && Materials[SlotIndex])
    {
        return Materials[SlotIndex];
    }
    return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

void USkeletalMesh::RebuildRenderData()
{
    RenderBounds = MeshData.AABB;
    LocalLODSphere.Center = (RenderBounds.Min + RenderBounds.Max) * 0.5f;

    double RadiusSquared = 0.0;
    for (const FSkeletalVertex& Vertex : MeshData.SkinnedVertices)
    {
        const double X = static_cast<double>(Vertex.Position.X) - LocalLODSphere.Center.X;
        const double Y = static_cast<double>(Vertex.Position.Y) - LocalLODSphere.Center.Y;
        const double Z = static_cast<double>(Vertex.Position.Z) - LocalLODSphere.Center.Z;
        RadiusSquared = std::max(RadiusSquared, X * X + Y * Y + Z * Z);
    }

    LocalLODSphere.RadiusSquared = std::nextafter(
        static_cast<float>(RadiusSquared * (1.0 + 2e-5)), std::numeric_limits<float>::infinity());

    for (FPrimitiveSceneProxy* Proxy : RenderProxies)
    {
        Proxy->OnMeshRenderDataChanged();
    }
}

void USkeletalMesh::RegisterProxy(FPrimitiveSceneProxy* Proxy)
{
    RenderProxies.Add(Proxy);
}

void USkeletalMesh::UnregisterProxy(FPrimitiveSceneProxy* Proxy)
{
    const auto It = std::find(RenderProxies.begin(), RenderProxies.end(), Proxy);
    if (It != RenderProxies.end())
    {
        RenderProxies.RemoveAtSwap(static_cast<uint32>(It - RenderProxies.begin()));
    }
}