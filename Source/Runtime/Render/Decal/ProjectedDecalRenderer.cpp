#include "EnginePCH.h"
#include "Render/Decal/ProjectedDecalRenderer.h"

#include "Render/Renderer.h"
#include "Render/MeshPassProcessor.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"

#include <array>

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

	if (!CreateTestTexture())
	{
		return false;
	}

	// 임시 테스트 설정
	TestSettings.Transform.Location =
		FVector(0.0f, 0.0f, 0.0f);

	TestSettings.Transform.Rotation =
		FRotator(0.0f, 0.0f, 0.0f);

	TestSettings.Transform.Scale =
		FVector(5.0f, 10.0f, 10.0f);

	TestSettings.TintOpacity =
		FVector4(1.0f, 1.0f, 1.0f, 0.8f);

	return IsReady();
}

bool FProjectedDecalRenderer::IsReady() const
{
	return
		Renderer &&
		PipelineState.Shader &&
		PipelineState.Shader->PixelShader &&
		ConstantBuffer &&
		ConstantBuffer->GetBuffer() &&
		TestTexture &&
		TestTexture->GetRawPtr() &&
		TestTexture->GetSRV();
}

bool FProjectedDecalRenderer::CreateTestTexture()
{
	constexpr uint32 TextureSize = 32;
	std::array<uint8, TextureSize* TextureSize * 4> Pixels{};

	for (uint32 Y = 0; Y < TextureSize; ++Y)
	{
		for (uint32 X = 0; X < TextureSize; ++X)
		{
			const float U =
				(static_cast<float>(X) + 0.5f) /
				static_cast<float>(TextureSize);

			const float V =
				(static_cast<float>(Y) + 0.5f) /
				static_cast<float>(TextureSize);

			const float DX = U - 0.5f;
			const float DY = V - 0.5f;

			const bool bInsideCircle =
				DX * DX + DY * DY < 0.45f * 0.45f;

			const bool bChecker =
				((X / 4 + Y / 4) & 1u) != 0;

			const uint32 Offset =
				(Y * TextureSize + X) * 4;

			Pixels[Offset + 0] = 255;
			Pixels[Offset + 1] = bChecker ? 210 : 40;
			Pixels[Offset + 2] = 20;
			Pixels[Offset + 3] =
				bInsideCircle ? 255 : 0;
		}
	}

	FTextureDesc Desc{};
	Desc.Width = TextureSize;
	Desc.Height = TextureSize;
	Desc.MipLevels = 1;

	Desc.Format =
		ETextureFormat::R8G8B8A8_UNorm_SRGB;

	Desc.BindFlags =
		ETextureBindFlags::ShaderResource;

	Desc.Usage =
		ETextureUsage::Immutable;

	TestTexture =
		RenderCommand::CreateTexture2D(
			Desc,
			Pixels.data());

	return
		TestTexture &&
		TestTexture->GetRawPtr() &&
		TestTexture->GetSRV();
}

bool FProjectedDecalRenderer::OnRender(
	std::span<const FMeshDrawBatch> ReceiverBatches,
	const FProjectedDecalTestSettings& Settings)
{
	// 그릴 것이 없는 것은 정상적인 성공으로 취급한다.
	if (!Settings.bEnabled ||
		Settings.TintOpacity.W <= 0.0f ||
		ReceiverBatches.empty())
	{
		return true;
	}

	if (!IsReady() ||
		!IsValidProjectedDecalTransform(Settings))
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

	const FProjectedDecalConstants Constants =
		MakeProjectedDecalConstants(Settings);

	RenderCommand::UpdateBufferData(
		ConstantBuffer.get(),
		&Constants,
		sizeof(Constants));

	RenderCommand::BindConstantBuffer(
		4,
		ConstantBuffer.get(),
		EShaderBindFlagBits::Pixel);


	RenderCommand::BindShaderResource(
		0,
		TestTexture.get(),
		EShaderBindFlagBits::Pixel);

	for (const FMeshDrawBatch& SourceBatch : ReceiverBatches)
	{
		if (SourceBatch.Items.IsEmpty())
			continue;

		FMeshDrawBatch Batch = SourceBatch;

		// Prepass와 같은 VS를 사용한다.
		// Lambert VS의 TEXCOORD1이 월드 위치.
		//FShaderProgram DecalProgram(
		//	SourceBatch.Pipeline.Shader->VertexShader,
		//	PipelineState.Shader->PixelShader);

		Batch.Pipeline = PipelineState;
		//Batch.Pipeline.Shader = &DecalProgram;

		// 원본 표면과 같은 컬링, 래스터 규칙을 유지한다.
		Batch.Pipeline.RasterizerState =
			SourceBatch.Pipeline.RasterizerState;

		Batch.bForceWireframe = false;

		Batch.bBindViewConstants = false;
		Batch.bUseObjectSlots = false;

		Batch.ObjectBindingSlot = 0;
		Batch.ObjectStages =
			EShaderBindFlagBits::Vertex |
			EShaderBindFlagBits::Pixel;

		// 원래 재질 텍스처가 데칼 t0를 덮어쓰지 않게 한다.
		Batch.Textures.Reset();

		Batch.Sampler = ESamplerState::LinearClamp;
		Batch.SamplerStages = EShaderBindFlagBits::Pixel;

		// 원본 MaterialData는 유지한다.
		// 재사용하는 VS의 UVTransform 등도 기존 값으로 바인딩한다.
		Renderer->DrawMeshes(
			std::span<const FMeshDrawBatch>(&Batch, 1));
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