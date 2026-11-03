#include "EnginePCH.h"
#include "PrimitiveSceneProxy.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BillboardComponent.h"
#include "Render/Mesh.h"
#include "Render/SkeletalMesh.h"
#include "Engine/Scene.h"

// ---------------- FPrimitiveSceneProxy (기본) ----------------
FPrimitiveSceneProxy::FPrimitiveSceneProxy(UPrimitiveComponent* InComponent)
    : Component(InComponent), bCastsShadow(InComponent ? InComponent->bCastShadow : true), bReceiveShadow(InComponent ? InComponent->bReceiveShadow : true), bReceiveDecal(InComponent ? InComponent->bReceiveDecal : true)
{
}

FPrimitiveSceneProxy::~FPrimitiveSceneProxy()
{
}

void FPrimitiveSceneProxy::UpdateTransform()
{
    LocalToWorld = Component->GetWorldMatrix();
    WorldToLocal = LocalToWorld.Inverse();
    const FBox LocalBounds = Component->CalcLocalBounds();
    Bounds = MakeWorldBounds(LocalBounds.GetWorldAABB(LocalToWorld));
    LODSphere = FLODSphere{};
}

void FPrimitiveSceneProxy::SetCastShadow(bool NewValue)
{
    bCastsShadow = NewValue;
}

void FPrimitiveSceneProxy::SetReceiveShadow(bool NewValue)
{
    bReceiveShadow = NewValue;
}

// ---------------- FStaticMeshSceneProxy ----------------
FStaticMeshSceneProxy::FStaticMeshSceneProxy(UStaticMeshComponent* InComponent)
    : FPrimitiveSceneProxy(InComponent)
{
}

FStaticMeshSceneProxy::~FStaticMeshSceneProxy()
{
    if (Mesh) Mesh->UnregisterProxy(this);
}

void FStaticMeshSceneProxy::OnMeshRenderDataChanged()
{
    if (Scene)
    {
        Scene->MarkRenderStateDirty(this);
        Scene->MarkDirty(this);
    }
}

void FStaticMeshSceneProxy::OnMeshDestroyed()
{
    Mesh = nullptr;
    RenderState.reset();
}

void FStaticMeshSceneProxy::UpdateTransform()
{
    LocalToWorld = Component->GetWorldMatrix();
    WorldToLocal = LocalToWorld.Inverse();
    const FBox LocalBounds = Mesh ? Mesh->GetRenderBounds() : Component->CalcLocalBounds();
    Bounds = MakeWorldBounds(LocalBounds.GetWorldAABB(LocalToWorld));
    LODSphere = Mesh ? Mesh->GetLocalLODSphere().Transform(LocalToWorld) : FLODSphere{};
}

void FStaticMeshSceneProxy::UpdateRenderState()
{
    UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
    UStaticMesh* PreviousMesh = Mesh;
    Mesh = StaticMeshComponent ? StaticMeshComponent->GetStaticMesh() : nullptr;
    RenderState.reset();
    bVisible = Component->IsShown(false);
    bHiddenInGame = !Component->IsShown(true);
    bCastsShadow = Component->bCastShadow;
    bReceiveShadow = Component->bReceiveShadow;
    bReceiveDecal = Component->bReceiveDecal;

    if (Mesh != PreviousMesh)
    {
        if (PreviousMesh) PreviousMesh->UnregisterProxy(this);
        if (Mesh) Mesh->RegisterProxy(this);
        if (Scene) Scene->MarkDirty(this);
    }
    if (!Mesh || !StaticMeshComponent) return;

    std::vector<UMaterial*> Materials;
    Materials.reserve(Mesh->MeshData.MaterialSlots.Num());
    for (uint32 Slot = 0; Slot < static_cast<uint32>(Mesh->MeshData.MaterialSlots.Num()); ++Slot)
        Materials.push_back(StaticMeshComponent->GetMaterial(Slot));
    RenderState = Mesh->GetRenderState(Materials);
}

FVertexBuffer* FStaticMeshSceneProxy::GetVertexBuffer(uint8 LOD) const
{
    return Mesh ? Mesh->GetVertexBuffer(LOD) : nullptr;
}

FIndexBuffer* FStaticMeshSceneProxy::GetIndexBuffer(uint8 LOD) const
{
    return Mesh ? Mesh->GetIndexBuffer(LOD) : nullptr;
}

// ---------------- FSkeletalMeshSceneProxy ----------------
FSkeletalMeshSceneProxy::FSkeletalMeshSceneProxy(USkeletalMeshComponent* InComponent)
    : FPrimitiveSceneProxy(InComponent)
{
}

FSkeletalMeshSceneProxy::~FSkeletalMeshSceneProxy()
{
    if (SkeletalMesh) SkeletalMesh->UnregisterProxy(this);
}

void FSkeletalMeshSceneProxy::OnMeshRenderDataChanged()
{
    if (Scene)
    {
        Scene->MarkRenderStateDirty(this);
        Scene->MarkDirty(this);
    }
}

void FSkeletalMeshSceneProxy::OnMeshDestroyed()
{
    SkeletalMesh = nullptr;
    RenderState.reset();
}

void FSkeletalMeshSceneProxy::UpdateTransform()
{
    LocalToWorld = Component->GetWorldMatrix();
    WorldToLocal = LocalToWorld.Inverse();
    const FBox LocalBounds = Component->CalcLocalBounds();
    Bounds = MakeWorldBounds(LocalBounds.GetWorldAABB(LocalToWorld));
    LODSphere = SkeletalMesh ? SkeletalMesh->GetLocalLODSphere().Transform(LocalToWorld) : FLODSphere{};
}

void FSkeletalMeshSceneProxy::UpdateRenderState()
{
    USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Component);
    USkeletalMesh* PreviousMesh = SkeletalMesh;
    SkeletalMesh = SkelComp ? SkelComp->GetSkeletalMesh() : nullptr;
    RenderState.reset();
    bVisible = Component->IsShown(false);
    bHiddenInGame = !Component->IsShown(true);
    bCastsShadow = Component->bCastShadow;
    bReceiveShadow = Component->bReceiveShadow;
    bReceiveDecal = Component->bReceiveDecal;

    if (SkeletalMesh != PreviousMesh)
    {
        if (PreviousMesh) PreviousMesh->UnregisterProxy(this);
        if (SkeletalMesh) SkeletalMesh->RegisterProxy(this);
        if (Scene) Scene->MarkDirty(this);
    }
    if (!SkeletalMesh || !SkelComp) return;

    auto NewState = std::make_shared<FMeshRenderState>();
    NewState->LODCount = 1;
    NewState->LODs[0].FirstSection = 0;
    NewState->LODs[0].NumSections = static_cast<uint32>(SkeletalMesh->MeshData.Sections.Num());

    NewState->Sections.SetNum(SkeletalMesh->MeshData.Sections.Num());
    for (int32 i = 0; i < SkeletalMesh->MeshData.Sections.Num(); ++i)
    {
        const auto& Sec = SkeletalMesh->MeshData.Sections[i];
        NewState->Sections[i].Material = SkelComp->GetMaterial(Sec.MaterialSlotIndex);
        NewState->Sections[i].StartIndex = Sec.StartIndex;
        NewState->Sections[i].IndexCount = Sec.IndexCount;
    }
    RenderState = NewState;
}

FVertexBuffer* FSkeletalMeshSceneProxy::GetVertexBuffer(uint8 LOD) const
{
    USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Component);
    return SkelComp ? SkelComp->GetSkinnedVertexBuffer() : nullptr;
}

FIndexBuffer* FSkeletalMeshSceneProxy::GetIndexBuffer(uint8 LOD) const
{
    return SkeletalMesh ? SkeletalMesh->IndexBuffer.get() : nullptr;
}

FBillboardSceneProxy::FBillboardSceneProxy(UBillboardComponent* InComponent)
    : FPrimitiveSceneProxy(InComponent)
{
}

void FBillboardSceneProxy::UpdateRenderState()
{
    RenderState.reset();
    bVisible = Component->IsShown(false);
    bHiddenInGame = !Component->IsShown(true);
    bCastsShadow = Component->bCastShadow;
    bReceiveShadow = Component->bReceiveShadow;
    bReceiveDecal = Component->bReceiveDecal;
}

FVertexBuffer* FBillboardSceneProxy::GetVertexBuffer(uint8 LOD) const
{
    UBillboardComponent* Billboard = Cast<UBillboardComponent>(Component);
    return (Billboard && Billboard->GetQuadMesh()) ? Billboard->GetQuadMesh()->GetVertexBuffer(LOD) : nullptr;
}

FIndexBuffer* FBillboardSceneProxy::GetIndexBuffer(uint8 LOD) const
{
    UBillboardComponent* Billboard = Cast<UBillboardComponent>(Component);
    return (Billboard && Billboard->GetQuadMesh()) ? Billboard->GetQuadMesh()->GetIndexBuffer(LOD) : nullptr;
}