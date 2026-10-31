#include "EnginePCH.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/RenderCommand.h"
#include "Render/RenderPacket.h"

USkeletalMeshComponent::USkeletalMeshComponent()
{
}

void USkeletalMeshComponent::BeginPlay()
{
    Super::BeginPlay();
    InitBoneTransforms();
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	if (bSkinningMatricesDirty)
	{
		PerformCPUSkinning();
	}
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	Super::SetSkeletalMesh(InSkeletalMesh);
	InitBoneTransforms();
}

void USkeletalMeshComponent::InitBoneTransforms()
{
	if (!SkeletalMesh) return;

	const TArray<FBoneInfo>& Bones = SkeletalMesh->MeshData.Bones;
	int32 BoneCount = Bones.Num();

	LocalBoneTransforms.SetNum(BoneCount);
	GlobalBoneTransforms.SetNum(BoneCount);
	SkinningMatrices.SetNum(BoneCount);

	for (int32 i = 0; i < BoneCount; ++i)
	{
		LocalBoneTransforms[i] = Bones[i].LocalBindPose;
		GlobalBoneTransforms[i] = Bones[i].GlobalBindPose;
		SkinningMatrices[i] = FMatrix::Identity; // 초기 상태는 T-Pose
	}

	bSkinningMatricesDirty = true;
	PerformCPUSkinning();
}

void USkeletalMeshComponent::SetBoneTransform(int32 BoneIndex, const FMatrix& LocalTransform)
{
	if (BoneIndex < 0 || BoneIndex >= LocalBoneTransforms.Num())
	{
		return;
	}

	LocalBoneTransforms[BoneIndex] = LocalTransform;
	bSkinningMatricesDirty = true;
}

void USkeletalMeshComponent::PerformCPUSkinning()
{
	if (!SkeletalMesh) return;
	const FSkeletalMeshData& MeshData = SkeletalMesh->MeshData;
	const auto& Bones = MeshData.Bones;
	int32 BoneCount = Bones.Num();
	if (BoneCount == 0) return;

	for (int32 i = 0; i < BoneCount; ++i)
	{
		int32 ParentIndex = Bones[i].ParentIndex;
		if (ParentIndex == -1)
		{	
			// 루트 본의 경우, 로컬 변환을 그대로 글로벌 변환으로 사용
			GlobalBoneTransforms[i] = LocalBoneTransforms[i];

		}
		else
		{
			// 부모 본의 글로벌 변환과 로컬 변환을 곱하여 글로벌 변환 계산
			GlobalBoneTransforms[i] = LocalBoneTransforms[i] * GlobalBoneTransforms[ParentIndex];
		}
		// 스키닝 매트릭스 계산
		SkinningMatrices[i] = Bones[i].InverseBindPose * GlobalBoneTransforms[i];
	}

	int32 VertexCount = static_cast<int32>(MeshData.SkinnedVertices.Num());
	SkinnedVertices.SetNum(VertexCount);

	for (int32 v = 0; v < VertexCount; ++v)
	{
		const FSkeletalVertex& Src = MeshData.SkinnedVertices[v];
		FVertex& Dst = SkinnedVertices[v];

		FVector BlendedPos(0.0f, 0.0f, 0.0f);
		FVector BlendedNormal(0.0f, 0.0f, 0.0f);

		for (int32 i = 0; i < 4; ++i)
		{
			int32 BoneIndex = Src.BoneIndices[i];
			float Weight = Src.BoneWeights[i];
			if (BoneIndex >= 0 && BoneIndex < BoneCount && Weight > 0.0f)
			{
				const FMatrix& SkinningMatrix = SkinningMatrices[BoneIndex];
				BlendedPos += SkinningMatrix.TransformPosition(Src.Position) * Weight;
				BlendedNormal += SkinningMatrix.TransformVector(Src.Normal) * Weight;
			}
		}
		Dst.Position = BlendedPos;
		Dst.Normal = BlendedNormal.GetSafeNormal();
		Dst.Tangent = Src.Tangent;
		Dst.UV = Src.UV;
		Dst.Color = Src.Color;
	}

	if (SkeletalMesh->VertexBuffer)
	{
		RenderCommand::UpdateBufferData(
			SkeletalMesh->VertexBuffer.get(),
			SkinnedVertices.GetData(),
			VertexCount * sizeof(FVertex)
		);
	}

	bSkinningMatricesDirty = false;
}

void USkeletalMeshComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
	if (!SkeletalMesh) return;
	const FSkeletalMeshData& MeshData = SkeletalMesh->MeshData;

	for (const FSkeletalMeshSection& Section : MeshData.Sections)
	{
		UMaterial* SectionMaterial = GetMaterial(static_cast<int32>(Section.MaterialSlotIndex));
		if (!SectionMaterial) continue;

		FRenderPacket Packet;
		Packet.SkeletalMesh = SkeletalMesh;
		Packet.Model = RenderQueue.StoreWorldMatrix(GetWorldMatrix());
		Packet.StartIndex = Section.StartIndex;
		Packet.IndexCount = Section.IndexCount;
		Packet.Material = SectionMaterial;
		RenderQueue.Add(Packet);
	}
}