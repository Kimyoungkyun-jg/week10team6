#pragma once

#include "Components/SceneComponent.h"
#include "Render/Decal/ProjectedDecal.h"

#include "DecalComponent.generated.h"

UCLASS()
class UDecalComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDecalComponent();
	~UDecalComponent() override = default;

	bool IsDecalEnabled() const
	{
		return bDecalEnabled;
	}

	void SetDecalEnabled(bool bInEnabled)
	{
		bDecalEnabled = bInEnabled;
	}

	void SetTintOpacity(const FVector4& InTintOpacity)
	{
		TintOpacity = InTintOpacity;
	}

	int32 GetSortOrder() const
	{
		return SortOrder;
	}

	void SetSortOrder(int32 InSortOrder)
	{
		SortOrder = InSortOrder;
	}

	// 현재 컴포넌트 값으로 렌더 데이터를 생성한다.
	// 표시 여부와 World 일치는 수집하는 쪽에서 확인한다.
	bool TryGetRenderData(
		FProjectedDecalRenderData& OutRenderData) const;

private:
	UPROPERTY(EditAnywhere)
	bool bDecalEnabled = true;

	UPROPERTY(EditAnywhere, Type=LinearColor)
	FVector4 TintOpacity =
		FVector4(1.0f, 1.0f, 1.0f, 0.8f);

	UPROPERTY(EditAnywhere)
	int32 SortOrder = 0;
};