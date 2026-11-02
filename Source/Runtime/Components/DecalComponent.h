#pragma once

#include "SceneComponent.h"
#include "Math/Vector4.h"
#include "Math/EngineMath.h"
#include "Render/Texture2D.h"
#include "DecalComponent.generated.h"

UCLASS()
class UDecalComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	virtual void OnTransformDirty() override;
	virtual void MarkRenderStateDirty() override;
	virtual void PostEditChangeProperty(const FProperty& Property) override;
	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
	FBox CalcLocalBounds() const override { return FBox{ FVector(-0.5f, -0.5f, -0.5f), FVector(0.5f, 0.5f, 0.5f) }; }
	UTexture2D* GetTexture() const { return Texture; }
	const FVector4& GetColor() const { return Color; }
	float GetOpacity() const { return Opacity; }
	bool ReceivesLighting() const { return bReceiveLighting; }
	bool ReceivesShadow() const { return bReceiveShadow; }
	void SetTexture(UTexture2D* InTexture) { Texture = InTexture; MarkRenderStateDirty(); }
	void SetColor(const FVector4& InColor) { Color = InColor; MarkRenderStateDirty(); }
	void SetOpacity(float InOpacity) { Opacity = FMath::Clamp(InOpacity, 0.0f, 1.0f); MarkRenderStateDirty(); }
	void SetReceiveLighting(bool bInReceiveLighting) { bReceiveLighting = bInReceiveLighting; MarkRenderStateDirty(); }
	void SetReceiveShadow(bool bInReceiveShadow) { bReceiveShadow = bInReceiveShadow; MarkRenderStateDirty(); }

	UPROPERTY(EditAnywhere)
	UTexture2D* Texture = nullptr;
	
	UPROPERTY(EditAnywhere, Type=LinearColor)
	FVector4 Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	
	UPROPERTY(EditAnywhere)
	float Opacity = 1.0f;
	
	UPROPERTY(EditAnywhere)
	bool bReceiveLighting = true;
	
	UPROPERTY(EditAnywhere)
	bool bReceiveShadow = true;

protected:
	bool ShouldCreateRenderState() const override { return true; }
	void CreateRenderState() override;
	void DestroyRenderState() override;

private:
	friend class FScene;
	class FDecalSceneProxy* SceneProxy = nullptr;
};
