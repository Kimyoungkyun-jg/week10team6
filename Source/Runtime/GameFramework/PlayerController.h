#pragma once

#include "GameFramework/Actor.h"
#include "Input/InputSystem.h"
#include "PlayerController.generated.h"

class APawn;
class APlayerCameraManager;
class UCurveFloat;
class AGameHUD;

UCLASS()
class APlayerController : public AActor
{
	GENERATED_BODY()
public:
	APlayerController();
	void Tick(float DeltaTime) override;
	void ProcessPlayerInput(float DeltaTime);
	void Possess(APawn* InPawn);
	void UnPossess();
	APawn* GetPawn() const { return PossessedPawn; }
	AGameHUD* GetHUD() const { return HUD; }
	void SetHUD(AGameHUD* InHUD) { HUD = InHUD; }
	void InputKey(int32 Key, bool bDown);
	void InputAxis(EGameInputAxis Axis, float Delta);
	void ClearInputState();

	FRotator ControlRotation;
	//PCM을 만들고 주인을 알려준다.
	void SpawnPlayerCameraManager();
	//Wolrd Tick으로부터 전달
	void UpdateCameraManager(float DeltaTime);
	APlayerCameraManager* GetCameraManager() const { return PlayerCameraManager; }
private:

	APlayerCameraManager* PlayerCameraManager = nullptr;
	APawn* PossessedPawn = nullptr;
	AGameHUD* HUD = nullptr;
	bool bKeyDown[256]{};
	float MouseAxisX = 0.0f;
	float MouseAxisY = 0.0f;
	float LookSensitivity = 0.1f;
};
