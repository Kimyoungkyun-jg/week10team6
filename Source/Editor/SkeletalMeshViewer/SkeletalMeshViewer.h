#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"

class UWorld;
class USkeletalMeshComponent;

// FBX 하나를 띄우는 창. 레벨과 섞이지 않도록 월드를 따로 갖는다.
struct FSkeletalMeshViewer
{
	~FSkeletalMeshViewer();

	FString Path;
	uint32 Id = 0;
	UWorld* World = nullptr;
	USkeletalMeshComponent* MeshComponent = nullptr;
	FEditorViewportClient Client;
	bool bOpen = true;
	bool bVisible = false;
};

class FSkeletalMeshViewerPanel : public IEditorPanel
{
public:
	bool Init() override { return true; }
	void Tick(float DeltaTime) override {}
	void OnRender() override;
	const char* GetPanelName() const override { return "Skeletal Mesh Viewer"; }
	void OpenFBX(const FString& Path);
	void Release();
	const TArray<TUniquePtr<FSkeletalMeshViewer>>& GetViewers() const { return Viewers; }

private:
	void DrawViewport(FSkeletalMeshViewer& Viewer);

	TArray<TUniquePtr<FSkeletalMeshViewer>> Viewers;
	uint32 NextId = 0;
};