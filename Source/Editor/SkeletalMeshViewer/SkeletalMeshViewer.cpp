#include "EnginePCH.h"
#include "SkeletalMeshViewer.h"
#include "Asset/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "Render/SkeletalMesh.h"

FSkeletalMeshViewer::~FSkeletalMeshViewer()
{
	Client.SetWorld(nullptr);
	if (World)
		GEngine->DestroyWorld(World);
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

	Viewers.Add(std::move(Viewer));
	SetOpen(true);
}

void FSkeletalMeshViewerPanel::Release()
{
	Viewers.Reset();
}

void FSkeletalMeshViewerPanel::OnRender()
{
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
			ImGui::BeginChild("Bones", ImVec2(250.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
			DrawBoneTree(*Viewer);
			ImGui::EndChild();
			ImGui::SameLine();
			DrawViewport(*Viewer);
		}
		ImGui::End();
	}
}

void FSkeletalMeshViewerPanel::DrawViewport(FSkeletalMeshViewer& Viewer)
{
	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if (Size.x < 1.0f || Size.y < 1.0f)
		return;

	// 이전 SRV가 들어가지 않도록 AddImage 전에 Resize
	Viewer.Client.Resize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));

	const ImVec2 Min = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##Viewport", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

	if (FTexture2D* Color = Viewer.Client.GetColorTarget())
		ImGui::GetWindowDrawList()->AddImage(Color->GetSRV(), Min, ImVec2(Min.x + Size.x, Min.y + Size.y));
	
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
		Viewer.SelectedBone = BoneIndex;

	if (bOpen && !bLeaf)
	{
		for (int32 Child : Viewer.Children[BoneIndex])
			DrawBoneNode(Viewer, Child);
		ImGui::TreePop();
	}
}