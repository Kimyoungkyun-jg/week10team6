#pragma once

#include "Math/Matrix.h"
#include "Math/Vector4.h"

struct FDecalViewConstants
{
	FMatrix DecalWorld;
	FMatrix ViewProjection;
	FMatrix InverseViewProjection;
	FMatrix InverseDecalWorld;
	FMatrix View;
	FVector4 ViewportInverseSize;
};

struct FDecalParameters
{
	FVector4 Color;
	FVector4 Options;
	FVector4 ShadowFlags;
	FVector4 SpecularColorShininess;
};

static_assert(sizeof(FDecalViewConstants) == sizeof(FMatrix) * 5 + sizeof(FVector4));
static_assert(sizeof(FDecalParameters) == sizeof(FVector4) * 4);
