#pragma once

#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "Math/Transform.h"
#include "Render/RenderStates.h"

#include <cmath>
#include <cstddef>

class FTexture2D;

// 컴포넌트에서 복사한 이번 뷰의 렌더입력
// 컴포넌트 자체의 포인터는 보관하지 않는다.
struct FProjectedDecalRenderData
{
	FMatrix DecalToWorld = FMatrix::Identity;

	// Material.BaseColor
	FVector4 TintOpacity = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

	// 에셋이 소유하는 텍스처의 비소유 포인터.
	FTexture2D* ColorTexture = nullptr;

	ESamplerState Sampler = ESamplerState::LinearClamp;

	// UNORM 색상 텍스처는 셰이더에서 선형화.
	// SRGB SRV는 하드웨어가 선형화하므로 false
	bool bDecodeSRGB = true;
	bool bUnlit = false;

	// 작은값부터 그린다. 큰 값이 나중에 덮인다.
	int32 SortOrder = 0;
	// 같은 SortOrder에서 실행 중 순서를 고정하는 용도.
	uint32 ComponentID = 0;
};

struct alignas(16) FProjectedDecalConstants
{
	FMatrix WorldToDecal;
	FVector4 TintOpacity; // rgb: 색상, a: 불투명도

	// X: 수동 sRGB 디코딩 여부, YZW: 예약
	FVector4 TextureParams;
};

struct alignas(16) FProjectedDecalDepthViewConstants
{
	FMatrix InverseViewProjection;
	FVector4 ViewportSize;
};

struct alignas(16) FProjectedDecalVolumeConstants
{
	FMatrix DecalToClip;
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
	Constants.TextureParams = FVector4(
		Data.bDecodeSRGB ? 1.0f : 0.0f,
		0.0f, 0.0f, 0.0f);

	return Constants;
}