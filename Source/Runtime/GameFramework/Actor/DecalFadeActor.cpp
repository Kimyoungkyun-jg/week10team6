#include "EnginePCH.h"
#include "DecalFadeActor.h"

#include <algorithm>

ADecalFadeActor::ADecalFadeActor()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ADecalFadeActor::BeginPlay()
{
	Super::BeginPlay();
	if (!HasBegunPlay()) return;

	UDecalComponent* Decal = Cast<UDecalComponent>(GetRootComponent());
	if (!Decal) return;

	TargetOpacity = Decal->GetOpacity();
	ElapsedTime = 0.0f;
	UpdateFade();
}

void ADecalFadeActor::Tick(float DeltaTime)
{
	ElapsedTime += std::max(DeltaTime, 0.0f);
	UpdateFade();
}

void ADecalFadeActor::UpdateFade()
{
	UDecalComponent* Decal = Cast<UDecalComponent>(GetRootComponent());
	if (!Decal) return;

	const float In = std::max(FadeInTime, 0.0f);
	const float Hold = std::max(MaintainTime, 0.0f);
	const float Out = std::max(FadeOutTime, 0.0f);
	float Fade = 0.0f;

	if (ElapsedTime < In)
		Fade = ElapsedTime / In;
	else if (ElapsedTime < In + Hold)
		Fade = 1.0f;
	else if (Out > 0.0f && ElapsedTime < In + Hold + Out)
		Fade = 1.0f - (ElapsedTime - In - Hold) / Out;
	else
		PrimaryActorTick.SetTickFunctionEnable(false);

	const float Opacity = TargetOpacity * Fade;
	if (Decal->GetOpacity() != Opacity) Decal->SetOpacity(Opacity);
}
