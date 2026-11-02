#include "EnginePCH.h"
#include "GameFramework/Actor/DecalActor.h"

ADecalActor::ADecalActor()
{
	DecalComponent = CreateDefaultSubobject<UDecalComponent>(
		"DecalComponent"
	);

	DecalComponent->SetupAttachment(GetRootComponent());
}