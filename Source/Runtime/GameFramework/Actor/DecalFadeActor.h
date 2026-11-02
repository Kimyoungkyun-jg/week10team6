#pragma once

#include "GameFramework/Actor/DecalActor.h"
#include "DecalFadeActor.generated.h"

UCLASS()
class ADecalFadeActor : public ADecalActor
{
	GENERATED_BODY()
public:
	ADecalFadeActor();
	void BeginPlay() override;
	void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere)
	float FadeInTime = 1.0f;
	UPROPERTY(EditAnywhere)
	float MaintainTime = 1.0f;
	UPROPERTY(EditAnywhere)
	float FadeOutTime = 1.0f;

private:
	void UpdateFade();
	float ElapsedTime = 0.0f;
	float TargetOpacity = 1.0f;
};
