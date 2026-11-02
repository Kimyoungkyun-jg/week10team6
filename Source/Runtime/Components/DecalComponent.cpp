#include "EnginePCH.h"
#include "Components/DecalComponent.h"
#include "Engine/Scene.h"
#include "Engine/World.h"

void UDecalComponent::CreateRenderState()
{
	Super::CreateRenderState();
	Opacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
	if (GetWorld()) GetWorld()->GetScene().AddDecal(this);
}

void UDecalComponent::DestroyRenderState()
{
	if (GetWorld()) GetWorld()->GetScene().RemoveDecal(this);
	Super::DestroyRenderState();
}

void UDecalComponent::OnTransformDirty()
{
	MarkRenderStateDirty();
}

void UDecalComponent::MarkRenderStateDirty()
{
	if (SceneProxy && GetWorld()) GetWorld()->GetScene().UpdateDecal(this);
}

void UDecalComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "Opacity") Opacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
	MarkRenderStateDirty();
}

void UDecalComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	if (Record.GetUnderlyingArchive().IsLoading())
	{
		Opacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
		MarkRenderStateDirty();
	}
}
