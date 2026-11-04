#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "GameFramework/Actor/SkeletalMeshActor.h"

#include <filesystem>
#include <functional>
#include <unordered_map>

namespace fs = std::filesystem;

class FContentDrawerPanel : public IEditorPanel
{
public:
	FContentDrawerPanel();
	virtual ~FContentDrawerPanel() override;
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Content Drawer"; }
	void RenderFolderTree(const fs::path& Path, fs::path& SelectedPath);
	void Release();

	// .widget 파일을 더블클릭하면 불린다 (UI 에디터가 연다).
	void SetOpenWidgetCallback(std::function<void(const FString&)> InCallback) { OnOpenWidget = std::move(InCallback); }
	void SetCreateWidgetCallback(std::function<void(const FString&)> InCallback) { OnCreateWidget = std::move(InCallback); }

	// .fbx 파일을 더블클릭 시
	void SetOpenSkeletalMeshCallback(std::function<void(const FString&)> InCallback) { OnOpenSkeletalMesh = std::move(InCallback); }

private:
	std::function<void(const FString&)> OnOpenWidget;
	std::function<void(const FString&)> OnCreateWidget;
	std::function<void(const FString&)> OnOpenSkeletalMesh;
	char NewAssetName[64] = "NewWidget";
	bool bOpenCreateWidget = false;

	fs::path AssetRootPath = "Assets";
	fs::path CurrentPath = "Assets";

	const FString FolderIconPath = "Icons/Folder.png";
	const FString FileIconPath = "Icons/File.png";
	
	std::unordered_map<FString, TUniquePtr<FTexture2D>> ThumbnailCache;
	UWorld* ThumbnailWorld = nullptr;
	ASkeletalMeshActor* ThumbnailActor = nullptr;
	TUniquePtr<class FSkyboxRenderer> ThumbnailSkybox;
	FEditorViewportClient ThumbnailClient;
	bool bStudioInitialized = false;
	void InitThumbnailStudio();
	FTexture2D* GetOrCreateSkeletalMeshThumbnail(const FString& FbxPath);
};