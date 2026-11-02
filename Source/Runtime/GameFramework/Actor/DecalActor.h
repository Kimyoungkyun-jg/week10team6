#pragma once 

#include "GameFramework/Actor.h"
#include "Components/DecalComponent.h"

#include "DecalActor.generated.h"

UCLASS()
class ADecalActor : public AActor
{
	GENERATED_BODY()

public:
	ADecalActor();
	~ADecalActor() override = default;

	UDecalComponent* GetDecalComponent() const
	{
		return DecalComponent;
	}

private:
	UDecalComponent* DecalComponent = nullptr;
};