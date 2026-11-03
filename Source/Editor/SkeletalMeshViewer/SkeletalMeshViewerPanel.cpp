#include "EnginePCH.h"
#include "SkeletalMeshViewerPanel.h"

#include "Asset/AssetManager.h"

#include "Engine/World.h"

#include "Editor/Gizmo/GizmoRenderer.h"

#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"

#include "Components/SkeletalMeshComponent.h"

#include "Render/SkeletalMesh.h"
#include "Render/LineBatcher.h"
#include "Render/Texture2D.h"
#include "Render/SkyboxRenderer.h"

#include "Collision/Ray.h"

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

	bool DrawAxisControl(const FString& _label, float& _value, float _speed, float _minValue, float _maxValue, float _resetValue, FVector4 _color)
	{
		bool isValueChanged = false;

		float lineHeight = GImGui->Font->LegacySize + GImGui->Style.FramePadding.y * 2.0f;
		ImVec4 color(_color.X, _color.Y, _color.Z, _color.W);
		ImVec2 buttonSize = { lineHeight + 2.0f , lineHeight };

		ImGui::PushStyleColor(ImGuiCol_Button, color);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ color.x + 0.1f, color.y + 0.1f, color.z + 0.1f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ color.x - 0.1f, color.y - 0.1f, color.z - 0.1f, 1.0f });

		if (ImGui::Button(_label.c_str(), buttonSize))
		{
			_value = _resetValue;
			isValueChanged = true;
		}
		ImGui::PopStyleColor(3);

		ImGui::SameLine();
		std::string dragID = "##" + _label;
		isValueChanged |= ImGui::DragFloat(dragID.c_str(), &_value, _speed, _minValue, _maxValue, "%.2f");

		return isValueChanged;
	}

	bool DrawVector3Controller(const FString& _label, float* _values, float _resetValue, float _columnWidth)
	{
		ImGuiIO& io = ImGui::GetIO();
		auto boldFont = io.Fonts->Fonts[0];

		ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
		bool isVectorChanged = false;

		ImGui::PushID(_label.c_str());
		if (ImGui::BeginTable(_label.c_str(), 2, flags))
		{
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, _columnWidth);
			ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text(_label.c_str());

			ImGui::TableSetColumnIndex(1);
			ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 2, 0 });
			ImGui::PushFont(boldFont);

			isVectorChanged |= DrawAxisControl("X", _values[0], 0.05f, 0.0f, 0.0f, _resetValue, { 0.8f, 0.1f, 0.1f, 1.0f });

			ImGui::PopItemWidth();

			ImGui::SameLine();
			isVectorChanged |= DrawAxisControl("Y", _values[1], 0.05f, 0.0f, 0.0f, _resetValue, { 0.1f, 0.8f, 0.1f, 1.0f });

			ImGui::PopItemWidth();

			ImGui::SameLine();
			isVectorChanged |= DrawAxisControl("Z", _values[2], 0.05f, 0.0f, 0.0f, _resetValue, { 0.1f, 0.1f, 0.8f, 1.0f });

			ImGui::PopItemWidth();

			ImGui::PopFont();
			ImGui::PopStyleVar();
			ImGui::EndTable();
		}

		ImGui::PopID();

		return isVectorChanged;
	}

	bool DrawRotatorAsXYZ(const FString& _label, FRotator& _rotator)
	{
		float Euler[3] = { _rotator.Roll, _rotator.Pitch, _rotator.Yaw };
		const bool isChanged = DrawVector3Controller(_label, Euler, 0.0f, 80.0f);
		if (isChanged)
		{
			_rotator.Roll = Euler[0];
			_rotator.Pitch = Euler[1];
			_rotator.Yaw = Euler[2];
		}
		return isChanged;
	}
}

bool FSkeletalMeshViewerPanel::Init()
{
	// 뷰어 전용 독립 월드 생성 (메인 레벨과 분리)
	PreviewWorld = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!PreviewWorld) return false;

	// 스켈레탈 메시 액터 스폰
	PreviewActor = PreviewWorld->SpawnActor<ASkeletalMeshActor>();
	if (!PreviewActor) return false;
	PreviewActor->GetSkeletalMeshComponent()->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
	MeshComponent = PreviewActor->GetSkeletalMeshComponent();

	// 기본 스카이라이트 조명 스폰
	if (ASkyLightActor* SkyLight = PreviewWorld->SpawnActor<ASkyLightActor>())
	{
		SkyLight->GetBillboardComponent()->SetVisibility(false);
		SkyLight->GetLightComponent()->SetIntensity(0.3f);
	}
	// 기본 디렉셔널라이트 스폰
	if (ADirectionalLightActor* DirLight = PreviewWorld->SpawnActor<ADirectionalLightActor>())
	{
		DirLight->GetBillboardComponent()->SetVisibility(false);
		DirLight->GetArrowComponent()->SetVisibility(false);
		DirLight->GetLightComponent()->SetIntensity(0.3f);
		DirLight->GetLightComponent()->SetRelativeRotation(FRotator(10.0f, 30.0f, -140.0f));
	}

	if (AStaticMeshActor* Floor = PreviewWorld->SpawnActor<AStaticMeshActor>())
	{
		Floor->GetStaticMeshComponent()->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Sphere"));
		Floor->GetStaticMeshComponent()->SetRelativeScale3D(FVector(10.0f, 10.0f, 1.0f));
		Floor->GetStaticMeshComponent()->SetRelativeLocation(FVector(0.0f, 0.0f, -0.5f));
	}

	// 뷰어 전용 스카이박스 초기화 (Assets/SkySphere/SkeletalMeshViewer.hdr)
	ViewerSkybox = MakeUnique<FSkyboxRenderer>();
	if (!ViewerSkybox->Init("Assets/SkySphere/SkeletalMeshViewer.hdr"))
	{
		ViewerSkybox.reset();
	}

	// 뷰포트 카메라 셋업
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
	ViewerSkybox.reset();
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

FSkyboxRenderer* FSkeletalMeshViewerPanel::GetSkybox() const
{
	return ViewerSkybox.get();
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

		const ImVec2 ImageMin = ImGui::GetItemRectMin();
		const ImVec2 MousePos = ImGui::GetMousePos();
		const FVector2 LocalMouse(MousePos.x - ImageMin.x, MousePos.y - ImageMin.y);

		if (!ImGui::GetIO().WantTextInput && FInputSystem::IsKeyPressed(EKeyCode::Space))
		{
			const int NextMode = (static_cast<int>(Gizmo.GetMode()) + 1) % 3;
			Gizmo.SetMode(static_cast<EGizmoMode>(NextMode));
		}

		const FRay MouseRay = Viewport.DeprojectScreenToWorld(LocalMouse);
		const bool bMouseDown = FInputSystem::IsMouseDown(EMouseButton::Left);
		Gizmo.Update(MouseRay, LocalMouse, Viewport.GetViewProjectionMatrix(),
			static_cast<int>(Size.x), static_cast<int>(Size.y),
			bMouseDown, Viewport.GetViewLocation(), false);

		if (ImGui::IsItemHovered() && bMouseDown && !Gizmo.IsUsing() && Gizmo.GetHoveredAxis() < 0)
		{
			PickBone(MouseRay);
		}

		if (!Gizmo.IsUsing() && ImGui::IsItemHovered())
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
	else
	{
		ImGui::Dummy(Size);
	}
}

void FSkeletalMeshViewerPanel::DrawDetails()
{
	ImGui::TextDisabled("--- DETAILS ---");
	if (SelectedBoneIndex < 0)
	{
		ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No Bone selected.");
		return;
	}

	const auto& Bones = CurrentMesh->MeshData.Bones;
	if (SelectedBoneIndex >= Bones.Num()) return;
	const FBoneInfo& Bone = Bones[SelectedBoneIndex];

	// 본 이름 및 부모 본 정보 출력
	ImGui::SeparatorText("Bone Info");
	ImGui::Text("Bone Name: %s", Bone.Name.c_str());
	ImGui::Text("Parent Bone: %s", (Bone.ParentIndex >= 0) ? Bones[Bone.ParentIndex].Name.c_str() : "None (Root)");

	// 로컬 트랜스폼
	ImGui::SeparatorText("Transform (Local)");
	FMatrix LocalMat = MeshComponent->GetBoneTransforms()[SelectedBoneIndex];
	FVector Loc = LocalMat.TransformPosition(FVector::ZeroVector);
	FRotator Rot = MatrixToRotator(LocalMat);
	FVector Scale(
		FVector(LocalMat.M[0][0], LocalMat.M[0][1], LocalMat.M[0][2]).Size(),
		FVector(LocalMat.M[1][0], LocalMat.M[1][1], LocalMat.M[1][2]).Size(),
		FVector(LocalMat.M[2][0], LocalMat.M[2][1], LocalMat.M[2][2]).Size()
	);
	if (Scale.X < 0.0001f) Scale.X = 1.0f;
	if (Scale.Y < 0.0001f) Scale.Y = 1.0f;
	if (Scale.Z < 0.0001f) Scale.Z = 1.0f;

	float LocValues[3] = { Loc.X, Loc.Y, Loc.Z };
	float ScaleValues[3] = { Scale.X, Scale.Y, Scale.Z };

	bool bModified = false;
	bModified |= DrawVector3Controller("Location", LocValues, 0.0f, 80.0f);
	bModified |= DrawRotatorAsXYZ("Rotation", Rot);
	bModified |= DrawVector3Controller("Scale", ScaleValues, 1.0f, 80.0f);

	if (bModified)
	{
		FTransform NewLocal(Rot, FVector(LocValues[0], LocValues[1], LocValues[2]), FVector(ScaleValues[0], ScaleValues[1], ScaleValues[2]));
		MeshComponent->SetBoneTransform(SelectedBoneIndex, NewLocal.GetLocalMatrix());
		MeshComponent->PerformCPUSkinning();
		SelectBone(SelectedBoneIndex);
	}

	// 컴포넌트 기준 글로벌 트랜스폼
	ImGui::SeparatorText("Transform (Component)");
	FMatrix GlobalMat = MeshComponent->GetGlobalBoneTransforms()[SelectedBoneIndex];
	FVector GLoc = GlobalMat.TransformPosition(FVector::ZeroVector);
	FRotator GRot = MatrixToRotator(GlobalMat);
	ImGui::TextDisabled("Location: (%.2f, %.2f, %.2f)", GLoc.X, GLoc.Y, GLoc.Z);
	ImGui::TextDisabled("Rotation: (%.1f, %.1f, %.1f)", GRot.Roll, GRot.Pitch, GRot.Yaw);
}

void FSkeletalMeshViewerPanel::DrawOverlay(FLineBatcher& Lines, FGizmoRenderer& GizmoRenderer, const FMatrix& ViewProjection)
{
	if (bShowBones && CurrentMesh && MeshComponent)
	{
		DrawBones(Lines, ViewProjection);
	}

	if (Gizmo.HasTarget())
	{
		GizmoRenderer.OnRender(Gizmo, ViewProjection, Viewport.GetViewLocation(), false);
	}
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
	if (BoneIndex >= 0 && CurrentMesh && MeshComponent)
	{
		FMatrix BoneWorld = MeshComponent->GetGlobalBoneTransforms()[BoneIndex] * MeshComponent->GetWorldMatrix();

		SelectedBoneTransform.Location = BoneWorld.TransformPosition(FVector::ZeroVector);
		SelectedBoneTransform.Rotation = MatrixToRotator(BoneWorld);
		SelectedBoneTransform.Scale = FVector(1.0f, 1.0f, 1.0f);

		Gizmo.SetTarget(&SelectedBoneTransform, [this, BoneIndex](const FTransform& NewTransform) {
			OnBoneTransformModified(BoneIndex, NewTransform);
		});
	}
	else
	{
		Gizmo.SetTarget(nullptr);
	}
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

void FSkeletalMeshViewerPanel::DrawBones(FLineBatcher& Lines, const FMatrix& ViewProjection) const
{
	if (!CurrentMesh || !MeshComponent) return;

	const TArray<FBoneInfo>& Bones = CurrentMesh->MeshData.Bones;
	const TArray<FMatrix>& Global = MeshComponent->GetGlobalBoneTransforms();
	if (Global.Num() != Bones.Num()) return;

	const FMatrix World = MeshComponent->GetWorldMatrix();
	const float MeshSize = std::max(0.01f, (CurrentMesh->MeshData.AABB.Max - CurrentMesh->MeshData.AABB.Min).Size());
	const float JointRadius = MeshSize * 0.007f;
	const FVector ViewLocation = Viewport.GetViewLocation();

	const FVector4 DefaultColor(0.2f, 0.7f, 1.0f, 1.0f);
	const FVector4 SelectedColor(1.0f, 0.9f, 0.1f, 1.0f);

	Lines.BeginFrame();

	for (int32 Bone = 0; Bone < Bones.Num(); ++Bone)
	{
		const FVector Head = (Global[Bone] * World).TransformPosition(FVector::ZeroVector);
		const bool bIsSelected = (Bone == SelectedBoneIndex);
		const FVector4 Color = bIsSelected ? SelectedColor : DefaultColor;

		// 1. 관절(Joint) 빌보드 원
		const FVector Facing = (ViewLocation - Head).GetSafeNormal();
		const FVector Reference = std::abs(Facing.Z) < 0.9f ? FVector(0, 0, 1) : FVector(0, 1, 0);
		const FVector Right = FVector::Cross(Facing, Reference).GetSafeNormal();
		const FVector Up = FVector::Cross(Facing, Right).GetSafeNormal();

		constexpr int32 Segments = 10;
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const float A = 2.0f * PI * Segment / Segments;
			const float B = 2.0f * PI * (Segment + 1) / Segments;
			Lines.AddLine(Head + (Right * std::cos(A) + Up * std::sin(A)) * JointRadius,
				Head + (Right * std::cos(B) + Up * std::sin(B)) * JointRadius, Color);
		}

		// 2. 리타겟과 동일한 3D 입체 다이아몬드(옥타헤드론) 뼈대 그리기
		const int32 Parent = Bones[Bone].ParentIndex;
		if (Parent < 0 || Parent >= Global.Num()) continue;

		const FVector Tail = (Global[Parent] * World).TransformPosition(FVector::ZeroVector);
		const FVector Axis = Head - Tail;
		const float Length = Axis.Size();
		if (Length <= MeshSize * 0.0001f) continue;

		const FVector Direction = Axis / Length;
		const FVector BoneReference = std::abs(Direction.Z) < 0.9f ? FVector(0, 0, 1) : FVector(0, 1, 0);
		const FVector Side = FVector::Cross(Direction, BoneReference).GetSafeNormal();
		const FVector BoneUp = FVector::Cross(Direction, Side).GetSafeNormal();

		const float Width = std::min(Length * 0.13f, MeshSize * 0.018f);
		const FVector Center = Tail + Axis * 0.25f;
		const FVector Ring[4] = {
			Center + Side * Width,
			Center + BoneUp * Width,
			Center - Side * Width,
			Center - BoneUp * Width
		};

		for (int32 Edge = 0; Edge < 4; ++Edge)
		{
			Lines.AddLine(Tail, Ring[Edge], Color);
			Lines.AddLine(Ring[Edge], Head, Color);
			Lines.AddLine(Ring[Edge], Ring[(Edge + 1) % 4], Color);
		}
	}

	Lines.OnRender(ViewProjection);
}

void FSkeletalMeshViewerPanel::OnBoneTransformModified(int32 BoneIndex, const FTransform& NewWorldTransform)
{
	if (!CurrentMesh || !MeshComponent || BoneIndex < 0) return;
	
	FMatrix NewWorldMatrix = NewWorldTransform.GetLocalMatrix();

	FMatrix CompWorldInverse = MeshComponent->GetWorldMatrix().Inverse();
	FMatrix NewGlobalMatrix = NewWorldMatrix * CompWorldInverse;

	int32 ParentIndex = CurrentMesh->MeshData.Bones[BoneIndex].ParentIndex;
	FMatrix NewLocalMatrix = NewGlobalMatrix;

	if (ParentIndex >= 0)
	{
		FMatrix ParentGlobalInverse = MeshComponent->GetGlobalBoneTransforms()[ParentIndex].Inverse();
		NewLocalMatrix = NewGlobalMatrix * ParentGlobalInverse;
	}

	MeshComponent->SetBoneTransform(BoneIndex, NewLocalMatrix);
	MeshComponent->PerformCPUSkinning();
}

bool FSkeletalMeshViewerPanel::PickBone(const FRay& Ray)
{
	if (!CurrentMesh || !MeshComponent) return false;
	const TArray<FMatrix>& Global = MeshComponent->GetGlobalBoneTransforms();
	const FMatrix World = MeshComponent->GetWorldMatrix();
	const float MeshSize = std::max(0.01f, (CurrentMesh->MeshData.AABB.Max - CurrentMesh->MeshData.AABB.Min).Size());
	const float JointRadius = MeshSize * 0.01f;
	int32 ClosestBoneIndex = -1;
	float ClosestDistance = FLT_MAX;
	for (int32 Bone = 0; Bone < Global.Num(); ++Bone)
	{
		const FVector Head = (Global[Bone] * World).TransformPosition(FVector::ZeroVector);
		float Distance;
		if (RayIntersectsSphere(Ray, Head, JointRadius, Distance))
		{
			if (Distance < ClosestDistance)
			{
				ClosestDistance = Distance;
				ClosestBoneIndex = Bone;
			}
		}
	}
	if (ClosestBoneIndex >= 0)
	{
		SelectBone(ClosestBoneIndex);
		return true;
	}
	else
	{
		SelectBone(-1);
	}
	return false;
}