#pragma once

#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "Math/Transform.h"

struct FProjectedDecalTestSettings
{
	bool bEnabled = true;

	// 월드 공간에서의 위치, 회전, 크기
	// Location: 데칼 볼륨의 중심
	// Rotation: 로컬 +X 축이 투영 방향
	// Scale.X: 투영 깊이
	// Scale.Y: 이미지 너비
	// Scale.Z: 이미지 높이
	// 로컬 볼륨은 XYZ 모두 [-0.5, +0.5]
	FTransform Transform = FTransform::Identity;

	// rgb: 색상, a: 불투명도
	FVector4 TintOpacity = FVector4(1.0f, 1.0f, 1.0f, 1.0f); 
};

struct alignas(16) FProjectedDecalConstants
{
	FMatrix WorldToDecal;
	FVector4 TintOpacity; // rgb: 색상, a: 불투명도
};

// 양수 스케일만 허용한다.
// 0에 가까운 스케일은 역행렬 계산 문제를 피하기 위해 제외한다.
inline bool IsValidProjectedDecalTransform(
	const FProjectedDecalTestSettings& Settings)
{
	constexpr float MinScale = 0.0001f;

	const FVector& Scale = Settings.Transform.Scale;

	return Scale.X >= MinScale && Scale.Y >= MinScale && Scale.Z >= MinScale;
}

// 호출전에 valid 검사 필요
inline FProjectedDecalConstants MakeProjectedDecalConstants(
	const FProjectedDecalTestSettings& Settings)
{
	FMatrix DecalToWorld = Settings.Transform.GetLocalMatrix();

	FProjectedDecalConstants Constants{};
	Constants.WorldToDecal = DecalToWorld.Inverse();
	Constants.TintOpacity = Settings.TintOpacity;

	return Constants;
}