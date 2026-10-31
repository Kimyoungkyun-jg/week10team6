#pragma once

#include "Asset/RenderAsset.h"
#include "Render/Buffer.h"
#include "Render/SkeletalMeshData.h"
#include "Render/MeshRenderData.h"
#include <unordered_map>
#include <vector>
#include "SkeletalMesh.generated.h"

class FPrimitiveSceneProxy;

UCLASS()
class USkeletalMesh : public URenderAsset
{
    GENERATED_BODY()

public:
    USkeletalMesh() : SortID(NextSortID++) {}
    virtual ~USkeletalMesh() override;

    FSkeletalMeshData MeshData;

    // Renderer가 바인드할 런타임 머티리얼 목록
    TArray<UMaterial*> Materials;

    TUniquePtr<FVertexBuffer> VertexBuffer;
    TUniquePtr<FIndexBuffer> IndexBuffer;

    const FSkeletalMeshData& GetMeshData() const { return MeshData; }
    UMaterial* GetMaterial(uint32 SlotIndex) const;

    void RebuildRenderData();
    const FLODSphere& GetLocalLODSphere() const { return LocalLODSphere; }
    const FBox& GetRenderBounds() const { return RenderBounds; }
    void RegisterProxy(FPrimitiveSceneProxy* Proxy);
    void UnregisterProxy(FPrimitiveSceneProxy* Proxy);

    const uint16 SortID;

private:
    static inline uint16 NextSortID = 0;

    FLODSphere LocalLODSphere;
    FBox RenderBounds;
    TArray<FPrimitiveSceneProxy*> RenderProxies;
};