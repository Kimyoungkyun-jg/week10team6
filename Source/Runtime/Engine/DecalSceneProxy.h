#pragma once

#include "Math/Box.h"
#include "Math/Matrix.h"
#include "Math/Vector4.h"

class UDecalComponent;
class UTexture2D;

class FDecalSceneProxy
{
public:
	explicit FDecalSceneProxy(UDecalComponent* InComponent);
	void Update();
	bool IsShown(bool bGameView) const { return bVisible && !(bGameView && bHiddenInGame); }
	bool HasValidTransform() const { return bInvertible; }
	bool IsRenderable(bool bGameView) const;

	UDecalComponent* GetComponent() const { return Component; }
	const FMatrix& GetWorldMatrix() const { return WorldMatrix; }
	const FMatrix& GetInverseWorldMatrix() const { return InverseWorldMatrix; }
	const FBox& GetBounds() const { return Bounds; }
	UTexture2D* GetTexture() const { return Texture; }
	const FVector4& GetColor() const { return Color; }
	float GetOpacity() const { return Opacity; }
	bool ReceivesLighting() const { return bReceiveLighting; }
	bool ReceivesShadow() const { return bReceiveShadow; }

private:
	UDecalComponent* Component = nullptr;
	FMatrix WorldMatrix;
	FMatrix InverseWorldMatrix;
	FBox Bounds;
	UTexture2D* Texture = nullptr;
	FVector4 Color;
	float Opacity = 1.0f;
	bool bReceiveLighting = true;
	bool bReceiveShadow = true;
	bool bVisible = true;
	bool bHiddenInGame = false;
	bool bInvertible = false;
};
