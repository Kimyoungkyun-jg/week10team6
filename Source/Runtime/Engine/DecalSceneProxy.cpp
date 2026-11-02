#include "EnginePCH.h"
#include "DecalSceneProxy.h"
#include "Components/DecalComponent.h"
#include "Render/Texture2D.h"

#include <cmath>

FDecalSceneProxy::FDecalSceneProxy(UDecalComponent* InComponent) : Component(InComponent)
{
	Update();
}

void FDecalSceneProxy::Update()
{
	WorldMatrix = Component->GetWorldMatrix();
	bInvertible = std::abs(WorldMatrix.Determinant()) >= 1.0e-8f;
	if (bInvertible) InverseWorldMatrix = WorldMatrix.Inverse();
	Bounds = FBox{ FVector(-0.5f, -0.5f, -0.5f), FVector(0.5f, 0.5f, 0.5f) }.GetWorldAABB(WorldMatrix);
	Texture = Component->GetTexture();
	Color = Component->GetColor();
	Opacity = Component->GetOpacity();
	bReceiveLighting = Component->ReceivesLighting();
	bReceiveShadow = Component->ReceivesShadow();
	bVisible = Component->IsShown(false);
	bHiddenInGame = !Component->IsShown(true);
}

bool FDecalSceneProxy::IsRenderable(bool bGameView) const
{
	return IsShown(bGameView) && bInvertible && Opacity > 0.0f &&
		Texture && Texture->GetResource() && Texture->GetResource()->GetSRV();
}
