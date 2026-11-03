#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Editor/Gizmo/Gizmo.h"

class UWorld;
class USkeletalMesh;
class USkeletalMeshComponent;
class ASkeletalMeshActor;
class FLineBatcher;
class FGizmoRenderer;

class FSkeletalMeshViewerPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Skeletal Mesh Viewer"; }
	void Release();

	// 렌더 패스 연동용 Getter
	UWorld* GetWorld() const { return PreviewWorld; }
	FEditorViewportClient& GetViewport() { return Viewport; }
	FGizmo& GetGizmo() { return Gizmo; }

	// 뷰포트 오버레이(본 와이어프레임 & 기즈모) 렌더링
	void DrawOverlay(FLineBatcher& Lines, FGizmoRenderer& GizmoRenderer, const FMatrix& ViewProjection);

	// 메시 설정 & 본 선택
	void SetSkeletalMesh(USkeletalMesh* InMesh);
	void SelectBone(int32 BoneIndex);

private:
	void DrawHierarchyTree();
	void DrawViewport();
	void DrawDetails();
	// 재귀적으로 본 노드를 그리는 함수
	void DrawBoneNode(int32 BoneIndex);
	// 부모-자식 계층 리스트 빌드
	void BuildBoneHierarchy();
	// 본 와이어 프레임 랜더링
	void DrawBones(FLineBatcher& Lines, const FMatrix& ViewProjection) const;
	// 기즈모 조작 시 본 트랜스폼 및 스키닝 갱신 콜백
	void OnBoneTransformModified(int32 BoneIndex, const FTransform& NewWorldTransform);
	// 레이 캐스트를 이용한 본 선택
	bool PickBone(const FRay& Ray);
	
	// 초기 레이아웃 설정
	void SetInitialLayout();

	// 본 계층 캐시 (각 본의 자식 인덱스 목록)
	TArray<TArray<int32>> BoneChildren;
	// 검색 필터 버퍼
	char BoneFilter[128] = "";

	// 3D 프리뷰 씬
	UWorld* PreviewWorld = nullptr;
	ASkeletalMeshActor* PreviewActor = nullptr;
	USkeletalMeshComponent* MeshComponent = nullptr;
	FEditorViewportClient Viewport;

	// 뷰어 전용 기즈모
	FGizmo Gizmo;

	// 현재 열린 메시
	USkeletalMesh* CurrentMesh = nullptr;

	// 본 조작 및 선택 상태
	int32 SelectedBoneIndex = -1;
	FTransform SelectedBoneTransform;

	// 표시 옵션
	bool bShowBones = true;
	bool bShowMesh = true;
	// 초기 레이아웃 설정 값
	bool bInitialLayoutSet = false;
	float InitialLeftTreeWidthRatio = 0.22f;
	float InitialRightDetailWidthRatio = 0.25f;

	// 검색 시 표시 여부 캐시
	TArray<uint8> BoneFilterVisible;
};