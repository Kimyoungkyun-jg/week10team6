#include "EnginePCH.h"
#include "Render/Decal/ProjectedDecalRenderer.h"

#include "Render/Renderer.h"
#include "Render/MeshPassProcessor.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"


bool FProjectedDecalRenderer::Init(FRenderer* InRenderer)
{
	assert(InRenderer);

	Renderer = InRenderer;

	if (!Renderer)
	{
		return false;
	}

	FPipelineState* BasePipeline =
		FRenderResourceManager::GetPSO(
			EPSOType::StaticMesh_DBufferDecal);

	if (!BasePipeline ||
		!BasePipeline->Shader ||
		!BasePipeline->Shader->VertexShader ||
		!BasePipeline->Shader->PixelShader)
	{
		return false;
	}

	PipelineState = *BasePipeline;

	ConstantBuffer =
		RenderCommand::CreateConstantBuffer(
			sizeof(FProjectedDecalConstants));

	if (!ConstantBuffer ||
		!ConstantBuffer->GetBuffer())
	{
		return false;
	}

	return IsReady();
}

bool FProjectedDecalRenderer::IsReady() const
{
	return
		Renderer &&
		PipelineState.Shader &&
		PipelineState.Shader->VertexShader &&
		PipelineState.Shader->PixelShader &&
		ConstantBuffer &&
		ConstantBuffer->GetBuffer();
}

bool FProjectedDecalRenderer::OnRender(
	std::span<const FMeshDrawBatch> ReceiverBatches,
	std::span<const FProjectedDecalRenderData> Decals,
	bool bUnlitPass)
{
	// 그릴 것이 없는 것은 정상적인 성공으로 취급한다.
	if (Decals.empty() ||
		ReceiverBatches.empty())
	{
		return true;
	}

	if (!IsReady())
	{
		return false;
	}

	// 바인딩 전에 필요한 셰이더를 확인한다.
	for (const FMeshDrawBatch& Batch : ReceiverBatches)
	{
		if (Batch.Items.IsEmpty())
			continue;

		if (!Batch.Pipeline.Shader ||
			!Batch.Pipeline.Shader->VertexShader)
		{
			return false;
		}
	}

	RenderCommand::BindConstantBuffer(
		4,
		ConstantBuffer.get(),
		EShaderBindFlagBits::Pixel);

	// SortOrder로 정렬된 순서.
	for (const FProjectedDecalRenderData& Decal : Decals)
	{
		if (Decal.bUnlit != bUnlitPass)
			continue;

		if (!IsValidProjectedDecalRenderData(Decal) ||
			!Decal.ColorTexture ||
			!Decal.ColorTexture->GetSRV())
		{
			continue;
		}

		const FProjectedDecalConstants Constants =
			MakeProjectedDecalConstants(Decal);

		RenderCommand::UpdateBufferData(
			ConstantBuffer.get(),
			&Constants,
			sizeof(Constants));

		// 데칼마다 다른 머티리얼 텍스처.
		RenderCommand::BindShaderResource(
			0,
			Decal.ColorTexture,
			EShaderBindFlagBits::Pixel);

		for (const FMeshDrawBatch& SourceBatch : ReceiverBatches)
		{
			if (SourceBatch.Items.IsEmpty())
				continue;

			FMeshDrawBatch Batch = SourceBatch;

			// 머티리얼의 PSO를 사용하지 않는다.
			Batch.Pipeline = PipelineState;

			// 원본 표면과 같은 컬링, 래스터 규칙을 유지한다.
			Batch.Pipeline.RasterizerState =
				SourceBatch.Pipeline.RasterizerState;

			Batch.bForceWireframe = false;

			Batch.bBindViewConstants = false;
			Batch.bUseObjectSlots = false;

			Batch.ObjectBindingSlot = 0;
			Batch.ObjectStages =
				EShaderBindFlagBits::Vertex;

			// 전용 데칼 셰이더는 원래 재질 상수를 사용하지 않는다.
			Batch.MaterialDataSize = 0;

			// 원래 재질 텍스처가 데칼 t0를 덮어쓰지 않게 한다.
			Batch.Textures.Reset();

			Batch.Sampler = ESamplerState::LinearClamp;
			Batch.SamplerStages = EShaderBindFlagBits::Pixel;

			// 원본 MaterialData는 유지한다.
			// 재사용하는 VS의 UVTransform 등도 기존 값으로 바인딩한다.
			Renderer->DrawMeshes(
				std::span<const FMeshDrawBatch>(&Batch, 1));
		}
	}

	RenderCommand::BindConstantBuffer(
		4,
		nullptr,
		EShaderBindFlagBits::Pixel);

	RenderCommand::UnbindShaderResources(
		0,
		1,
		EShaderBindFlagBits::Pixel);

	return true;
}