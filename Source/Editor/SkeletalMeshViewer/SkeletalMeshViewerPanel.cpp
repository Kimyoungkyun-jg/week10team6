#include "EnginePCH.h"
#include "SkeletalMeshViewerPanel.h"
#include "Asset/AssetManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "Render/LineBatcher.h"
#include "Render/Texture2D.h"
#include "Editor/Gizmo/GizmoRenderer.h"

#include <algorithm>
#include <cctype>

namespace
{
	// 대소문자 무시 부분 문자열 검색 함수
	bool ContainsIgnoreCase(const std::string& Source, const char* Query)
	{
		if (!Query || Query[0] == '\0') return true;
		std::string s = Source;
		std::string q = Query;
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
		std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return std::tolower(c); });
		return s.find(q) != std::string::npos;
	}
}

bool FSkeletalMeshViewerPanel::Init()
{
	// 1. 뷰어 전용 독립 월드 생성 (메인 레벨과 분리)
	PreviewWorld = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!PreviewWorld) return false;

	// 2. 스켈레탈 메시 액터 스폰
	PreviewActor = PreviewWorld->SpawnActor<ASkeletalMeshActor>();
	if (!PreviewActor) return false;
	MeshComponent = PreviewActor->GetSkeletalMeshComponent();

	// 3. 기본 스카이라이트 조명 스폰
	if (ASkyLightActor* SkyLight = PreviewWorld->SpawnActor<ASkyLightActor>())
	{
		SkyLight->GetBillboardComponent()->SetVisibility(false);
	}

	// 4. 뷰포트 카메라 셋업
	Viewport.SetWorld(PreviewWorld);
	Viewport.SetViewportType(ELevelViewportType::Perspective);
	Viewport.Resize(1, 1);
	Viewport.SetActive(true);

	// 테스트용 기본 캐릭터 메시 로드 (Mint.fbx)
	USkeletalMesh* DefaultMesh = UAssetManager::LoadFbxSkeletalMesh("Assets/Models/Mint/Mint.fbx");
	if (DefaultMesh)
	{
		SetSkeletalMesh(DefaultMesh);
	}

	return true;
}

void FSkeletalMeshViewerPanel::Release()
{
	Viewport.SetWorld(nullptr);
	if (PreviewWorld)
	{
		PreviewWorld->ClearWorld();
		PreviewWorld->FlushPendingDestruction();
	}
	PreviewWorld = nullptr;
	PreviewActor = nullptr;
	MeshComponent = nullptr;
	CurrentMesh = nullptr;
}

void FSkeletalMeshViewerPanel::Tick(float DeltaTime)
{
	if (!IsOpen()) return;

	if (PreviewWorld)
	{
		PreviewWorld->Tick(DeltaTime);
	}
}

void FSkeletalMeshViewerPanel::SetInitialLayout()
{
	if (bInitialLayoutSet) return;
	const float TotalW = ImGui::GetWindowWidth();
	const float LeftTreeWidth = TotalW * InitialLeftTreeWidthRatio;
	const float RightDetailWidth = TotalW * InitialRightDetailWidthRatio;
	ImGui::SetColumnOffset(1, LeftTreeWidth);
	ImGui::SetColumnOffset(2, TotalW - RightDetailWidth);
	bInitialLayoutSet = true;
}

void FSkeletalMeshViewerPanel::OnRender()
{
	bool bOpen = IsOpen();
	if (!ImGui::Begin(GetPanelName(), &bOpen))
	{
		ImGui::End();
		SetOpen(bOpen);
		return;
	}

	ImGui::Columns(3, "ViewerColumns", true);

	// 초기 레이아웃 설정
	SetInitialLayout();

	// 1. 왼쪽: 본 트리
	DrawHierarchyTree();
	ImGui::NextColumn();

	// 2. 중앙: 3D 뷰포트
	DrawViewport();
	ImGui::NextColumn();

	// 3. 오른쪽: 본 디테일
	DrawDetails();

	ImGui::Columns(1);
	ImGui::End();
	SetOpen(bOpen);
}

void FSkeletalMeshViewerPanel::BuildBoneHierarchy()
{
	BoneChildren.Reset();
	if (!CurrentMesh) return;
	const auto& Bones = CurrentMesh->MeshData.Bones;
	BoneChildren.SetNum(Bones.Num());
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		int32 Parent = Bones[i].ParentIndex;
		if (Parent >= 0 && Parent < Bones.Num())
		{
			BoneChildren[Parent].Add(i);
		}
	}
}

void FSkeletalMeshViewerPanel::DrawViewport()
{
	const ImVec2 Available = ImGui::GetContentRegionAvail();
	const ImVec2 Size(std::max(1.0f, Available.x), std::max(1.0f, Available.y - 2.0f));

	Viewport.Resize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));
	Viewport.SetRect(FRect{ 0, 0, Size.x, Size.y });

	FTexture2D* Color = Viewport.GetColorTarget();
	if (Color && Color->GetSRV())
	{
		ImGui::Image((ImTextureID)Color->GetSRV(), Size);
	}
	else
	{
		ImGui::Dummy(Size);
	}

	if (ImGui::IsItemHovered())
	{
		const float Wheel = ImGui::GetIO().MouseWheel;
		if (Wheel != 0.0f)
		{
			const FVector Location = Viewport.GetViewLocation();
			Viewport.SetViewLocation(Location + Viewport.GetViewForward() * (Wheel * 10.0f));
		}

		Viewport.TickInput(ImGui::GetIO().DeltaTime,
			FInputSystem::IsMouseDown(EMouseButton::Right), true, 0, 10.0f, 0.2f);
	}
}

void FSkeletalMeshViewerPanel::DrawDetails()
{
	ImGui::TextDisabled("--- DETAILS ---");
}

void FSkeletalMeshViewerPanel::DrawOverlay(FLineBatcher& Lines, FGizmoRenderer& GizmoRenderer, const FMatrix& ViewProj)
{

}

void FSkeletalMeshViewerPanel::SetSkeletalMesh(USkeletalMesh* InMesh)
{
	CurrentMesh = InMesh;
	if (MeshComponent)
	{
		MeshComponent->SetSkeletalMesh(InMesh);
	}
	BuildBoneHierarchy();
	SelectBone(-1);
	if (CurrentMesh)
	{
		const FBox& Bounds = CurrentMesh->MeshData.AABB;
		const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
		const float Radius = std::max(0.5f, (Bounds.Max - Bounds.Min).Size() * 0.5f);

		Viewport.SetViewRotation(FRotator(0.0f, 180.0f, 0.0f));
		Viewport.SetViewLocation(Center - Viewport.GetViewForward() * (Radius * 2.5f));
	}
}

void FSkeletalMeshViewerPanel::SelectBone(int32 BoneIndex)
{
	SelectedBoneIndex = BoneIndex;
}

void FSkeletalMeshViewerPanel::DrawHierarchyTree()
{
	ImGui::TextDisabled("--- SKELETON TREE ---");

	if (!CurrentMesh)
	{
		ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No Skeletal Mesh loaded.");
		return;
	}

	if (ImGui::Button("Reset Pose", ImVec2(-1, 0)))
	{
		if (MeshComponent)
			MeshComponent->InitBoneTransforms();
	}

	ImGui::SetNextItemWidth(-1);
	ImGui::InputTextWithHint("##BoneSearch", "Search Bones...", BoneFilter, sizeof(BoneFilter));
	ImGui::Separator();

	const auto& Bones = CurrentMesh->MeshData.Bones;
	const bool bHasFilter = (BoneFilter[0] != '\0');

	BoneFilterVisible.Init(0, Bones.Num());
	if (bHasFilter)
	{
		for (int32 i = 0; i < Bones.Num(); ++i)
		{
			if (ContainsIgnoreCase(Bones[i].Name, BoneFilter))
			{
				int32 Curr = i;
				while (Curr >= 0 && Curr < Bones.Num())
				{
					BoneFilterVisible[Curr] = true;
					Curr = Bones[Curr].ParentIndex;
				}
			}
		}
	}
	else
	{
		for (int32 i = 0; i < Bones.Num(); ++i)
			BoneFilterVisible[i] = true;
	}

	ImGui::BeginChild("BoneTreeScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		if (Bones[i].ParentIndex == -1)
		{
			DrawBoneNode(i);
		}
	}

	ImGui::EndChild();
}

void FSkeletalMeshViewerPanel::DrawBoneNode(int32 BoneIndex)
{
	if (!BoneFilterVisible[BoneIndex]) return;

	const auto& Bones = CurrentMesh->MeshData.Bones;
	const FBoneInfo& Bone = Bones[BoneIndex];
	const bool bHasChildren = BoneChildren[BoneIndex].Num() > 0;
	const bool bHasFilter = (BoneFilter[0] != '\0');

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (!bHasChildren) Flags |= ImGuiTreeNodeFlags_Leaf;
	if (SelectedBoneIndex == BoneIndex) Flags |= ImGuiTreeNodeFlags_Selected;

	if (BoneIndex == 0 || bHasFilter)
		Flags |= ImGuiTreeNodeFlags_DefaultOpen;

	bool bNodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)BoneIndex, Flags, "%s", Bone.Name.c_str());

	if (ImGui::IsItemClicked())
	{
		SelectBone(BoneIndex);
	}

	if (bNodeOpen)
	{
		for (int32 ChildIndex : BoneChildren[BoneIndex])
		{
			DrawBoneNode(ChildIndex);
		}
		ImGui::TreePop();
	}
}