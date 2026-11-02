#pragma once

#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "Math/Transform.h"
// 컴포넌트에서 복사한 이번 뷰의 렌더입력
// 컴포넌트 자체의 포인터는 보관하지 않는다.
struct FProjectedDecalRenderData
{
	FMatrix DecalToWorld = FMatrix::Identity;

	FVector4 TintOpacity = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

	// 작은값부터 그린다. 큰 값이 나중에 덮인다.
	int32 SortOrder = 0;
	// 같은 SortOrder에서 실행 중 순서를 고정하는 용도.
	uint32 ComponentID = 0;
};

struct alignas(16) FProjectedDecalConstants
{
	FMatrix WorldToDecal;
	FVector4 TintOpacity; // rgb: 색상, a: 불투명도
};

// 양수 스케일만 허용한다.
// 0에 가까운 스케일은 역행렬 계산 문제를 피하기 위해 제외한다.
inline bool IsValidProjectedDecalRenderData(
	const FProjectedDecalRenderData& Data)
{
	for (int32 Row = 0; Row < 4; ++Row)
	{
		for (int32 Column = 0; Column < 4; ++Column)
		{
			if (!std::isfinite(
				Data.DecalToWorld.M[Row][Column]))
			{
				return false;
			}
		}
	}

	const float Determinant = Data.DecalToWorld.Determinant();

	return
		std::isfinite(Determinant) &&
		std::abs(Determinant) > 1e-12f;
}

// 호출전에 valid 검사 필요
inline FProjectedDecalConstants MakeProjectedDecalConstants(
	const FProjectedDecalRenderData& Data)
{
	FProjectedDecalConstants Constants{};
	Constants.WorldToDecal = Data.DecalToWorld.Inverse();
	Constants.TintOpacity = Data.TintOpacity;

	return Constants;
}