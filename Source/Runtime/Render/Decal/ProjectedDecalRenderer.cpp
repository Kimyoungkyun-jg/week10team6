#include "EnginePCH.h"
#include "Render/Decal/ProjectedDecalRenderer.h"

#include "Render/Renderer.h"
#include "Render/MeshPassProcessor.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/SceneView.h"
#include "Render/Decal/DecalStats.h"


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

	MeshPipelineState = *BasePipeline;

	ConstantBuffer =
		RenderCommand::CreateConstantBuffer(
			sizeof(FProjectedDecalConstants));

	if (!ConstantBuffer ||
		!ConstantBuffer->GetBuffer())
	{
		return false;
	}

	FPipelineState* DepthPipeline =
		FRenderResourceManager::GetPSO(
			EPSOType::DBufferDecal_DepthReconstruction);

	if (!DepthPipeline ||
		!DepthPipeline->Shader ||
		!DepthPipeline->Shader->VertexShader ||
		!DepthPipeline->Shader->PixelShader)
	{
		return false;
	}

	DepthPipelineState = *DepthPipeline;

	DepthViewConstantBuffer =
		RenderCommand::CreateConstantBuffer(
			sizeof(FProjectedDecalDepthViewConstants));

	if (!DepthViewConstantBuffer ||
		!DepthViewConstantBuffer->GetBuffer())
	{
		return false;
	}

	return IsReady();
}

bool FProjectedDecalRenderer::IsReady() const
{
	return
		Renderer &&
		MeshPipelineState.Shader &&
		MeshPipelineState.Shader->VertexShader &&
		MeshPipelineState.Shader->PixelShader &&
		ConstantBuffer &&
		ConstantBuffer->GetBuffer() &&
		DepthPipelineState.Shader &&
		DepthPipelineState.Shader->VertexShader &&
		DepthPipelineState.Shader->PixelShader &&
		DepthViewConstantBuffer &&
		DepthViewConstantBuffer->GetBuffer();
}

bool FProjectedDecalRenderer::OnRenderMesh(
	std::span<const FMeshDrawBatch> ReceiverBatches,
	std::span<const FProjectedDecalRenderData> Decals,
	bool bUnlitPass,
	FDecalFrameStats* Stats)
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

	bool bSubmitted = false;

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
			Batch.Pipeline = MeshPipelineState;

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

			Batch.Sampler = Decal.Sampler;
			Batch.SamplerStages = EShaderBindFlagBits::Pixel;


			if (Stats)
			{
				for (const FMeshDrawBatch::FItem& Item : Batch.Items)
				{
					++Stats->DrawCalls;
					Stats->SubmittedTriangles += Item.IndexCount / 3;
				}
			}
			// 원본 MaterialData는 유지한다.
			// 재사용하는 VS의 UVTransform 등도 기존 값으로 바인딩한다.
			Renderer->DrawMeshes(
				std::span<const FMeshDrawBatch>(&Batch, 1));

			bSubmitted = true;
		}

		if (Stats && bSubmitted)
		{
			if (bUnlitPass)
				++Stats->SubmittedUnlit;
			else
				++Stats->SubmittedLit;
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

bool FProjectedDecalRenderer::OnRenderDepth(
	const FSceneView& View,
	uint32 Width,
	uint32 Height,
	std::span<const FProjectedDecalRenderData> Decals,
	bool bUnlitPass,
	FDecalFrameStats* Stats,
	bool bUseReceiverStencil)
{
	if (Decals.empty())
		return true;

	if (!IsReady() || Width == 0 || Height == 0)
		return false;

	FProjectedDecalDepthViewConstants ViewConstants{};

	ViewConstants.InverseViewProjection =
		View.ViewProjectionMatrix.Inverse();

	ViewConstants.ViewportSize = FVector4(
		static_cast<float>(Width),
		static_cast<float>(Height),
		1.0f / static_cast<float>(Width),
		1.0f / static_cast<float>(Height));

	RenderCommand::UpdateBufferData(
		DepthViewConstantBuffer.get(),
		&ViewConstants,
		sizeof(ViewConstants));

	// 파이프라인 바인딩
	{
		FPipelineState Pipeline = DepthPipelineState;
		Pipeline.DepthStencilState =
			bUseReceiverStencil
			? EDepthStencilState::DecalReceiverTest
			: EDepthStencilState::Disabled;
		Pipeline.StencilRef =
			bUseReceiverStencil
			? StencilBits::ReceiveDecal
			: 0;

		RenderCommand::BindPipelineState(
			Pipeline);
	}

	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::BindIndexBuffer(nullptr);

	RenderCommand::BindConstantBuffer(
		5,
		DepthViewConstantBuffer.get(),
		EShaderBindFlagBits::Pixel);

	RenderCommand::BindConstantBuffer(
		4,
		ConstantBuffer.get(),
		EShaderBindFlagBits::Pixel);

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

		RenderCommand::BindShaderResource(
			0,
			Decal.ColorTexture,
			EShaderBindFlagBits::Pixel);

		RenderCommand::BindSamplerState(
			0,
			Decal.Sampler,
			EShaderBindFlagBits::Pixel);

		RenderCommand::Draw(3);

		if (Stats)
		{
			++Stats->DrawCalls;
			++Stats->SubmittedTriangles;

			if (bUnlitPass)
				++Stats->SubmittedUnlit;
			else
				++Stats->SubmittedLit;
		}
	}

	RenderCommand::BindConstantBuffer(
		4,
		nullptr,
		EShaderBindFlagBits::Pixel);

	RenderCommand::BindConstantBuffer(
		5,
		nullptr,
		EShaderBindFlagBits::Pixel);

	// t0: 데칼 텍스처
	// t1: RenderGraph가 바인딩한 SceneDepth
	RenderCommand::UnbindShaderResources(
		0,
		2,
		EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);

	return true;
}