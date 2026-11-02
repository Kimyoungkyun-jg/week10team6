#pragma once

#include "Render/Buffer.h"
#include "Render/PipelineState.h"
#include "Render/Texture2D.h"
#include "Render/Decal/ProjectedDecal.h"

#include <span>

class FRenderer;
struct FMeshDrawBatch;

class FProjectedDecalRenderer
{
public:
	bool Init(FRenderer* InRenderer);

	bool IsReady() const;

	// 호출 전제:
	// 1. DBuffer RTV가 바인딩되어 있다.
	// 2. Prepass Detpth가 DSV로 바인딩되어 있다.
	// 3. ReceiverBatches는 Lambert Forward 배치다.
	//
	// 렌더 타깃 설정과 Clear는 RenderGraphs가 담당한다.
	bool OnRender(
		std::span<const FMeshDrawBatch> ReceiverBatches,
		const FProjectedDecalTestSettings& Settings);

	// 컴포넌트 연결 전까지 사용할 임시 테스트 설정.
	FProjectedDecalTestSettings& GetTestSettings() { return TestSettings; }
	const FProjectedDecalTestSettings& GetTestSettings() const { return TestSettings; }

private:
	bool CreateTestTexture();

	// 비소유 포인터.
	FRenderer* Renderer = nullptr;

	// PSO
	FPipelineState PipelineState;

	TUniquePtr<FConstantBuffer> ConstantBuffer;
	TUniquePtr<FTexture2D> TestTexture;

	FProjectedDecalTestSettings TestSettings;
};