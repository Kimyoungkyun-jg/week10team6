#include "EnginePCH.h"
#include "Components/DecalComponent.h"

#include <algorithm>
#include <cmath>

#include "Render/LineBatcher.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/Actor.h"

UDecalComponent::UDecalComponent()
{
	// 로컬 박스: XYZ 모두 [-0.5, +0.5]
	//
	// X: 투영 깊이
	// Y: 이미지 너비
	// Z: 이미지 높이
	SetRelativeScale3D(FVector(5.0f, 10.0f, 10.0f));
}

bool UDecalComponent::TryGetRenderData(
	FProjectedDecalRenderData& OutRenderData) const
{
	if (!bDecalEnabled)
		return false;

	if (!std::isfinite(TintOpacity.X) ||
		!std::isfinite(TintOpacity.Y) ||
		!std::isfinite(TintOpacity.Z) ||
		!std::isfinite(TintOpacity.W))
	{
		return false;
	}

	const float Opacity =
		std::clamp(TintOpacity.W, 0.0f, 1.0f);

	if (Opacity <= 0.0f)
		return false;

	// 양수 월드 스케일만 지원한다.
	const FVector WorldScale = GetWorldScale3D();

	constexpr float MinScale = 1e-4f;

	if (!std::isfinite(WorldScale.X) ||
		!std::isfinite(WorldScale.Y) ||
		!std::isfinite(WorldScale.Z) ||
		WorldScale.X < MinScale ||
		WorldScale.Y < MinScale ||
		WorldScale.Z < MinScale)
	{
		return false;
	}

	FProjectedDecalRenderData Data{};

	// 상대 Transform이 아닌 부모까지 반영된 월드 행렬.
	Data.DecalToWorld = GetWorldMatrix();

	Data.TintOpacity = FVector4(
		std::max(0.0f, TintOpacity.X),
		std::max(0.0f, TintOpacity.Y),
		std::max(0.0f, TintOpacity.Z),
		Opacity);

	Data.SortOrder = SortOrder;
	Data.ComponentID = GetUUID();

	if (!IsValidProjectedDecalRenderData(Data))
		return false;

	OutRenderData = Data;
	return true;
}

void DrawDecalVolume(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected)
{
	if (!World || !LineBatcher) return;

	for (TObjectIterator<UDecalComponent> It; It; ++It)
	{
		UDecalComponent* Decal = *It;

		if (!Decal ||
			Decal->GetWorld() != World ||
			!Decal->IsRegistered())
		{
			continue;
		}

		LineBatcher->AddOrientedBox(
			Decal->CalcLocalBounds(),
			Decal->GetWorldMatrix(),
			FVector4(0.0f, 1.0f, 1.0f, 1.0f));
	}
}