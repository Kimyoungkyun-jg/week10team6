#include "EnginePCH.h"
#include "SkeletalMeshViewer.h"
#include "Asset/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "Render/SkeletalMesh.h"
#include "Collision/Ray.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	FTransform DecomposeMatrix(const FMatrix& M)
	{
		FTransform Out;
		Out.Location = FVector(M[3][0], M[3][1], M[3][2]);

		const FVector X(M[0][0], M[0][1], M[0][2]);
		const FVector Y(M[1][0], M[1][1], M[1][2]);
		const FVector Z(M[2][0], M[2][1], M[2][2]);
		
		Out.Scale = FVector(X.Size(), Y.Size(), Z.Size());

		FMatrix Rotation = FMatrix::Identity;
		Rotation.SetAxis(0, X.Normalized());
		Rotation.SetAxis(1, Y.Normalized());
		Rotation.SetAxis(2, Z.Normalized());
		Out.Rotation = MatrixToRotator(Rotation);

		return Out;
	}
}

FSkeletalMeshViewer::~FSkeletalMeshViewer()
{
	delete BoneGizmoTarget;
	Client.SetWorld(nullptr);
	if (World)
		GEngine->DestroyWorld(World);
}

void FSkeletalMeshViewerPanel::Tick(float DeltaTime)
{
	for (TUniquePtr<FSkeletalMeshViewer>& Viewer : Viewers)
	{
		if (Viewer->ViewSize.x >= 1.0f && Viewer->ViewSize.y >= 1.0f)
			Viewer->Client.Resize(static_cast<uint32>(Viewer->ViewSize.x), static_cast<uint32>(Viewer->ViewSize.y));
	}
}

void FSkeletalMeshViewerPanel::OpenFBX(const FString& Path)
{
	USkeletalMesh* Mesh = UAssetManager::LoadFbxSkeletalMesh(Path);
	if (!Mesh)
		return;

	TUniquePtr<FSkeletalMeshViewer> Viewer = MakeUnique<FSkeletalMeshViewer>();
	Viewer->Path = Path;
	Viewer->Id = NextId++;

	// World 구성
	Viewer->World = UWorld::CreateWorld(EWorldType::Editor, false);
	Viewer->MeshComponent = Viewer->World->SpawnActor<ASkeletalMeshActor>()->GetSkeletalMeshComponent();
	Viewer->MeshComponent->SetSkeletalMesh(Mesh);

	const TArray<FBoneInfo>& Bones = Mesh->MeshData.Bones;
	Viewer->Children.SetNum(Bones.Num());
	for (int32 i = 0; i < Bones.Num(); ++i)
		if (Bones[i].ParentIndex >= 0)
			Viewer->Children[Bones[i].ParentIndex].Add(i);

	if (ASkyLightActor* SkyLight = Viewer->World->SpawnActor<ASkyLightActor>())
		SkyLight->GetBillboardComponent()->SetVisibility(false);

	Viewer->Client.SetWorld(Viewer->World);
	Viewer->Client.SetViewportType(ELevelViewportType::Perspective);

	// Mesh가 한 화면에 들어오게 카메라를 뒤로 이동 (FOV 60 기준)
	const FBox Bounds = Viewer->MeshComponent->CalcBounds();
	const float Radius = (Bounds.Max - Bounds.Min).Size() * 0.5f;
	Viewer->Client.SetViewLocation((Bounds.Min + Bounds.Max) * 0.5f - Viewer->Client.GetViewForward() * (Radius * 3.0f));

	Viewer->BoneGizmoTarget = NewObject<USceneComponent>();

	Viewers.Add(std::move(Viewer));
	SetOpen(true);
}

void FSkeletalMeshViewerPanel::Release()
{
	Viewers.Reset();
}

void FSkeletalMeshViewerPanel::OnRender()
{
	bHasFocusedViewer = false;

	// x 버튼으로 닫은 뷰어는 다음 프레임 맨 앞에서 지운다
	for (int32 i = Viewers.Num() - 1; i >= 0; --i)
		if (!Viewers[i]->bOpen)
			Viewers.RemoveAt(i);

	for (TUniquePtr<FSkeletalMeshViewer>& Viewer : Viewers)
	{
		const FString Title = Viewer->Path + "###SkeletalMeshViewer" + std::to_string(Viewer->Id);
		ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f), ImGuiCond_FirstUseEver);
		Viewer->bVisible = ImGui::Begin(Title.c_str(), &Viewer->bOpen);
		if (Viewer->bVisible)
		{
			const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			bHasFocusedViewer |= bFocused;
			if (bFocused && !Viewer->Gizmo.IsUsing() && ImGui::IsKeyPressed(ImGuiKey_Space, false))
			{
				const int NextMode = (static_cast<int>(Viewer->Gizmo.GetMode()) + 1) % 3;
				Viewer->Gizmo.SetMode(static_cast<EGizmoMode>(NextMode));
			}

			ImGui::BeginChild("Bones", ImVec2(250.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
			const float DetailsHeight = ImGui::GetFrameHeightWithSpacing() * 6.0f;
			ImGui::BeginChild("Tree", ImVec2(0.0f, -DetailsHeight));
			DrawBoneTree(*Viewer);
			ImGui::EndChild();
			ImGui::Separator();
			DrawBoneDetails(*Viewer);
			ImGui::EndChild();
			ImGui::SameLine();
			DrawViewport(*Viewer);
		}
		ImGui::End();
	}
}

void FSkeletalMeshViewerPanel::DrawBoneDetails(FSkeletalMeshViewer& Viewer)
{
	USkeletalMeshComponent* Mesh = Viewer.MeshComponent;
	if (ImGui::Button("Reset Pose"))
		Mesh->InitBoneTransforms();

	if (Viewer.SelectedBone < 0)
	{
		ImGui::TextDisabled("Select a bone");
		return;
	}

	const TArray<FBoneInfo>& Bones = Mesh->GetSkeletalMesh()->MeshData.Bones;
	const FBoneInfo& Bone = Bones[Viewer.SelectedBone];
	ImGui::TextUnformatted(Bone.Name.c_str());
	ImGui::TextDisabled("Parent: %s", Bone.ParentIndex >= 0 ? Bones[Bone.ParentIndex].Name.c_str() : "(root)");

	// 부모 기준 로컬 트랜스폼을 표시
	FTransform Local = DecomposeMatrix(Mesh->GetBoneTransforms()[Viewer.SelectedBone]);
	float Euler[3] = { Local.Rotation.Roll, Local.Rotation.Pitch, Local.Rotation.Yaw };

	bool bChanged = false;
	bChanged |= ImGui::DragFloat3("Location", Local.Location.V, 0.01f);
	bChanged |= ImGui::DragFloat3("Rotation", Euler, 0.5f);
	bChanged |= ImGui::DragFloat3("Scale", Local.Scale.V, 0.01f);

	if (bChanged)
	{
		Local.Rotation = FRotator(Euler[1], Euler[2], Euler[0]);
		Mesh->SetBoneTransform(Viewer.SelectedBone, Local.GetLocalMatrix());
		Mesh->PerformCPUSkinning();
	}

	if (ImGui::Button("Reset Bone"))
	{
		Mesh->SetBoneTransform(Viewer.SelectedBone, Bone.LocalBindPose);
		Mesh->PerformCPUSkinning();
	}
}

void FSkeletalMeshViewerPanel::DrawViewport(FSkeletalMeshViewer& Viewer)
{
	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if (Size.x < 1.0f || Size.y < 1.0f)
		return;

	// 이전 SRV가 들어가지 않도록 AddImage 전에 조정
	// 실제 Resize는 다음 프레임 Tick에서.
	Viewer.ViewSize = Size;

	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const bool bClicked = ImGui::InvisibleButton("##Viewport", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

	if (FTexture2D* Color = Viewer.Client.GetColorTarget())
		ImGui::GetWindowDrawList()->AddImage(Color->GetSRV(), Min, ImVec2(Min.x + Size.x, Min.y + Size.y));

	const int32 HoveredJoint = DrawSkeleton(Viewer, Min, Size);

	if (bClicked && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !Viewer.Gizmo.IsUsing())
	{
		SelectBone(Viewer, HoveredJoint);
		Viewer.bScrollTreeToSelection = HoveredJoint >= 0;
	}
	
	const bool bLeftDown = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
	UpdateBoneGizmo(Viewer, Min, bLeftDown);

	// 오른쪽 버튼 + 마우스 회전
	// WASD/QE 이동
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
		Viewer.Client.TickInput(ImGui::GetIO().DeltaTime, ImGui::IsMouseDown(ImGuiMouseButton_Right), true, 0, 10.0f, 0.2f);
}

void FSkeletalMeshViewerPanel::DrawBoneTree(FSkeletalMeshViewer& Viewer)
{
	const TArray<FBoneInfo>& Bones = Viewer.MeshComponent->GetSkeletalMesh()->MeshData.Bones;
	for (int32 i = 0; i < Bones.Num(); ++i)
		if (Bones[i].ParentIndex < 0)
			DrawBoneNode(Viewer, i); // Root부터 재귀로 내려간다
}

void FSkeletalMeshViewerPanel::DrawBoneNode(FSkeletalMeshViewer& Viewer, int32 BoneIndex)
{
	const FBoneInfo& Bone = Viewer.MeshComponent->GetSkeletalMesh()->MeshData.Bones[BoneIndex];
	const bool bLeaf = Viewer.Children[BoneIndex].IsEmpty();

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
		ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_DrawLinesToNodes;

	if (BoneIndex == Viewer.SelectedBone)
		Flags |= ImGuiTreeNodeFlags_Selected;

	// 자식이 없으면 TreePush를 하지 않으므로 아래에서 TreePop도 안 한다
	if (bLeaf)
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

	// Bone 이름이 겹칠 수 있으므로 ID는 Index로
	const bool bOpen = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(BoneIndex)), Flags, "%s", Bone.Name.c_str());
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		SelectBone(Viewer, BoneIndex);

	if (BoneIndex == Viewer.SelectedBone && Viewer.bScrollTreeToSelection)
	{
		ImGui::SetScrollHereY(0.5f);
		Viewer.bScrollTreeToSelection = false;
	}

	if (bOpen && !bLeaf)
	{
		for (int32 Child : Viewer.Children[BoneIndex])
			DrawBoneNode(Viewer, Child);
		ImGui::TreePop();
	}
}

int32 FSkeletalMeshViewerPanel::DrawSkeleton(FSkeletalMeshViewer& Viewer, const ImVec2& ViewMin, const ImVec2& ViewSize)
{
	const TArray<FBoneInfo>& Bones = Viewer.MeshComponent->GetSkeletalMesh()->MeshData.Bones;
	const TArray<FMatrix>& Globals = Viewer.MeshComponent->GetGlobalBoneTransforms();
	if (Globals.Num() != Bones.Num())
		return -1;

	const FMatrix ComponentWorld = Viewer.MeshComponent->GetWorldMatrix();
	const FMatrix ViewProj = Viewer.Client.GetViewProjectionMatrix();

	TArray<FVector2> Joints;
	Joints.SetNum(Bones.Num());
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		const FVector Location = (Globals[i] * ComponentWorld).TransformPosition(FVector::ZeroVector);
		Joints[i] = WorldToScreen(Location, ViewProj, static_cast<int>(ViewSize.x), static_cast<int>(ViewSize.y));
	}

	const auto IsBehind = [&](int32 i) { return Joints[i].X == -FLT_MAX;};
	const auto ToScreen = [&](int32 i) { return ImVec2(ViewMin.x + Joints[i].X, ViewMin.y + Joints[i].Y); };

	const ImVec2 Mouse = ImGui::GetMousePos();
	int32 HoveredJoint = -1;
	float BestDistSq = 8.0f * 8.0f;
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		if (IsBehind(i))
			continue;
		const ImVec2 P = ToScreen(i);
		const float DistSq = (P.x - Mouse.x) * (P.x - Mouse.x) + (P.y - Mouse.y) * (P.y - Mouse.y);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			HoveredJoint = i;
		}
	}
	
	ImDrawList* DL = ImGui::GetWindowDrawList();
	DL->PushClipRect(ViewMin, ImVec2(ViewMin.x + ViewSize.x, ViewMin.y + ViewSize.y), true);
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		if (IsBehind(i))
			continue;

		const int32 Parent = Bones[i].ParentIndex;
		if (Parent >= 0 && !IsBehind(Parent))
			DL->AddLine(ToScreen(Parent), ToScreen(i), IM_COL32(220, 220, 220, 180), 1.5f);

		const bool bSelected = (i == Viewer.SelectedBone);
		const bool bHovered = (i == HoveredJoint);
		const ImU32 Color = bSelected 
			? IM_COL32(255, 170, 40, 255)
			: (bHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(100, 190, 255, 255));
		DL->AddCircleFilled(ToScreen(i), (bSelected || bHovered) ? 5.0f : 3.0f, Color);
	}
	DL->PopClipRect();
	return HoveredJoint;
}

void FSkeletalMeshViewerPanel::SelectBone(FSkeletalMeshViewer& Viewer, int32 BoneIndex)
{
	Viewer.SelectedBone = BoneIndex;
	Viewer.Gizmo.SetTarget(BoneIndex >= 0 ? Viewer.BoneGizmoTarget : nullptr);
}

void FSkeletalMeshViewerPanel::UpdateBoneGizmo(FSkeletalMeshViewer& Viewer, const ImVec2& ViewMin, bool bLeftDown)
{
	if (Viewer.SelectedBone < 0)
		return;

	USkeletalMeshComponent* Mesh = Viewer.MeshComponent;
	const TArray<FMatrix>& Globals = Mesh->GetGlobalBoneTransform(); // 모델 공간
	const FMatrix ComponentWorld = Mesh->GetWorldMatrix();

	if (!Viewer.Gizmo.IsUsing())
		Viewer.BoneGizmoTarget->SetWorldTransform(Globals[Viewer.SelectedBone] * ComponentWorld, false);

	const ImVec2 Mouse = ImGui::GetMousePos();
	const FVector2 MousePos(Mouse.x - ViewMin.x, Mouse.y - ViewMin.y);
	FEditorViewportClient& Client = Viewer.Client;
	Viewer.Gizmo.Update(
		Client.DeprojectScreenToWorld(MousePos), MousePos,
		Client.GetViewProjectionMatrix(),
		static_cast<int>(Client.GetWidth()), static_cast<int>(Client.GetHeight()),
		bLeftDown, Client.GetViewLocation(), !Client.IsPerspective()
	);

	// Gizmo가 움직인 타겟을 Bone 로컬 트랜스폼으로 되돌린다
	if (Viewer.Gizmo.IsUsing())
	{
		const int32 Parent = Mesh->GetSkeletalMesh()->MeshData.Bones[Viewer.SelectedBone].ParentIndex;
		const FMatrix ParentWorld = (Parent >= 0 ? Globals[Parent] : FMatrix::Identity) * ComponentWorld;
		Mesh->SetBoneTransform(Viewer.SelectedBone, Viewer.BoneGizmoTarget->GetWorldMatrix() * ParentWorld.Inverse());
		Mesh->PerformCPUSkinning();
	}
}