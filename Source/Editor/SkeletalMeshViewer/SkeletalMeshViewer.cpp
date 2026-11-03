#include "EnginePCH.h"
#include "SkeletalMeshViewer.h"
#include "Asset/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "GameFramework/Actor/SkyLightActor.h"

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
			DrawViewport(*Viewer);
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