#include "EnginePCH.h"
#include "Render/Decal/ProjectedDecalRenderer.h"

#include "Render/Renderer.h"
#include "Render/MeshPassProcessor.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/SceneView.h"
#include "Render/Decal/DecalStats.h"

namespace
{
	struct FDecalVolumeVertex
	{
		float X;
		float Y;
		float Z;
	};

	constexpr FDecalVolumeVertex GDecalVolumeVertices[] =
	{
		{ -0.5f, -0.5f, -0.5f },
		{ 0.5f, -0.5f, -0.5f },
		{ 0.5f,  0.5f, -0.5f },
		{ -0.5f,  0.5f, -0.5f },

		{ -0.5f, -0.5f,  0.5f },
		{ 0.5f, -0.5f,  0.5f },
		{ 0.5f,  0.5f,  0.5f },
		{ -0.5f,  0.5f,  0.5f },
	};

	// 바깥쪽을 앞면으로 정의.
	constexpr uint32 GDecalVolumeIndices[] =
	{
		// -Z
		0, 2, 1,
		0, 3, 2,

		// +Z
		4, 5, 6,
		4, 6, 7,

		// -Y
		0, 1, 5,
		0, 5, 4,

		// +Y
		3, 7, 6,
		3, 6, 2,

		// -X
		0, 4, 7,
		0, 7, 3,

		// +X
		1, 2, 6,
		1, 6, 5,
	};

	enum class EDecalDepthGeometry
	{
		Volume,
		FullscreenFar,
		Culled,
		Invalid,
	};

	EDecalDepthGeometry ChooseDecalDepthGeometry(
		const FProjectedDecalRenderData& Decal,
		const FSceneView& View,
		const FMatrix& DecalToClip)
	{
		// 같은 클립 평면 밖에 모든 꼭짓점이 있으면 제외.
		bool bOutsideLeft = true;
		bool bOutsideRight = true;
		bool bOutsideBottom = true;
		bool bOutsideTop = true;
		bool bOutsideNear = true;
		bool bOutsideFar = true;

		for (const FDecalVolumeVertex& Vertex : GDecalVolumeVertices)
		{
			const FVector4 Clip =
				DecalToClip.TransformFVector4(FVector4(Vertex.X, Vertex.Y, Vertex.Z, 1.0f));

			if (!std::isfinite(Clip.X) ||
				!std::isfinite(Clip.Y) ||
				!std::isfinite(Clip.Z) ||
				!std::isfinite(Clip.W))
			{
				return EDecalDepthGeometry::Invalid;
			}

			bOutsideLeft &= Clip.X < -Clip.W;
			bOutsideRight &= Clip.X > Clip.W;
			bOutsideBottom &= Clip.Y < -Clip.W;
			bOutsideTop &= Clip.Y > Clip.W;
			bOutsideNear &= Clip.Z < 0.0f;
			bOutsideFar &= Clip.Z > Clip.W;
		}

		// 모든 꼭짓점이 클립 평면 밖이면 제외.
		if (bOutsideLeft ||
			bOutsideRight ||
			bOutsideBottom ||
			bOutsideTop ||
			bOutsideNear ||
			bOutsideFar)
		{
			return EDecalDepthGeometry::Culled;
		}

		// 현제 엔진은 View 공간 X가 깊이.
		const FMatrix DecalToView = Decal.DecalToWorld * View.ViewMatrix;

		// 로컬 반크기 0.5인 박스의 View 공간 깊이 범위 계산.
		const float CenterDepth = DecalToView.M[3][0];

		const float ExtentDepth =
			0.5f * (
				std::abs(DecalToView.M[0][0])
				+ std::abs(DecalToView.M[1][0])
				+ std::abs(DecalToView.M[2][0]));

		const float MaxDepth = CenterDepth + ExtentDepth;

		if (!std::isfinite(MaxDepth) ||
			!std::isfinite(View.FarClip) ||
			View.FarClip <= 0.0f)
		{
			return EDecalDepthGeometry::Invalid;
		}

		// Far Plane 직전에도 작은 안전 여유를 둔다.
		const float FarMargin =
			std::max(1e-3f, View.FarClip * 1e-6f);

		// Far Plane 밖에 있으면 전체 화면으로 처리한다.
		if (MaxDepth >= View.FarClip - FarMargin)
			return EDecalDepthGeometry::FullscreenFar;

		
		return EDecalDepthGeometry::Volume;
	}
}

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

	if (!InitVolumeResources())
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
		DepthViewConstantBuffer->GetBuffer() &&
		IsVolumeResourcesReady();
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

	RenderCommand::BindConstantBuffer(
		5,
		DepthViewConstantBuffer.get(),
		EShaderBindFlagBits::Pixel);

	RenderCommand::BindConstantBuffer(
		4,
		ConstantBuffer.get(),
		EShaderBindFlagBits::Pixel);

	// 전체 화면에서는 일반 깊이 검사를 하지 않는다.
	FPipelineState FullscreenPipeline = DepthPipelineState;

	FullscreenPipeline.RasterizerState =
		ERasterizerState::SolidNone;

	FullscreenPipeline.DepthStencilState =
		bUseReceiverStencil
		? EDepthStencilState::DecalReceiverTest
		: EDepthStencilState::Disabled;

	FullscreenPipeline.StencilRef =
		bUseReceiverStencil
		? StencilBits::ReceiveDecal
		: 0;

	// 볼륨에서는 출구 면과 SceneDepth를 비교한다.
	FPipelineState VolumePipeline = VolumePipelineState;

	VolumePipeline.DepthStencilState =
		bUseReceiverStencil
		? EDepthStencilState::DecalVolumeGreaterEqualReceiver
		: EDepthStencilState::DecalVolumeGreaterEqual;

	VolumePipeline.StencilRef =
		bUseReceiverStencil
		? StencilBits::ReceiveDecal
		: 0;

	enum class EBoundGeometry
	{
		None,
		VolumeNormal,
		VolumeMirrored,
		Fullscreen,
	};

	EBoundGeometry Bound = EBoundGeometry::None;

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

		const FMatrix DecalToClip =
			Decal.DecalToWorld * View.ViewProjectionMatrix;

		const EDecalDepthGeometry Geometry =
			ChooseDecalDepthGeometry(Decal, View, DecalToClip);

		if (Geometry == EDecalDepthGeometry::Culled)
		{
			if (Stats)
				++Stats->FrustumRejectedDecals;

			continue;
		}

		if (Geometry == EDecalDepthGeometry::Invalid)
		{
			if (Stats)
				++Stats->InvalidGeometryDecals;

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

		if (Geometry == EDecalDepthGeometry::Volume)
		{
			// 반사 변환에서는 삼각형 방향도 반전된다.
			const bool bMirrored = Decal.DecalToWorld.Determinant() < 0.0f;

			const EBoundGeometry Desired =
				bMirrored
				? EBoundGeometry::VolumeMirrored
				: EBoundGeometry::VolumeNormal;

			// 이전 바인딩과 다른 경우에만 파이프라인을 바꾼다.
			if (Bound != Desired)
			{
				FPipelineState Pipeline = VolumePipeline;

				Pipeline.RasterizerState =
					bMirrored
					? ERasterizerState::SolidBack
					: ERasterizerState::SolidFront;

				RenderCommand::BindPipelineState(Pipeline);

				RenderCommand::BindVertexBuffer(
					VolumeVertexBuffer.get());
				RenderCommand::BindIndexBuffer(
					VolumeIndexBuffer.get());

				RenderCommand::BindConstantBuffer(
					6,
					VolumeConstantBuffer.get(),
					EShaderBindFlagBits::Vertex);

				Bound = Desired;
			}

			FProjectedDecalVolumeConstants VolumeConstants{};
			VolumeConstants.DecalToClip = DecalToClip;

			RenderCommand::UpdateBufferData(
				VolumeConstantBuffer.get(),
				&VolumeConstants,
				sizeof(VolumeConstants));

			RenderCommand::DrawIndexed(
				VolumeIndexBuffer->GetIndexCount());

			if (Stats)
			{
				++Stats->VolumeDrawCalls;

				Stats->SubmittedTriangles +=
					VolumeIndexBuffer->GetIndexCount() / 3;
			}
		}
		else
		{
			if (Bound != EBoundGeometry::Fullscreen)
			{
				RenderCommand::BindPipelineState(FullscreenPipeline);

				RenderCommand::BindVertexBuffer(nullptr);
				RenderCommand::BindIndexBuffer(nullptr);

				Bound = EBoundGeometry::Fullscreen;
			}

			RenderCommand::Draw(3);

			if (Stats)
			{
				++Stats->FarFallbackDrawCalls;
				++Stats->SubmittedTriangles;
			}
		}

		if (Stats)
		{
			++Stats->DrawCalls;

			if (bUnlitPass)
				++Stats->SubmittedUnlit;
			else
				++Stats->SubmittedLit;
		}
	}

	RenderCommand::BindConstantBuffer(
		6,
		nullptr,
		EShaderBindFlagBits::Vertex);

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

bool FProjectedDecalRenderer::InitVolumeResources()
{
	FShaderProgram* Program =
		FRenderResourceManager::GetShaderProgram(
			"Resources/Shader/ProjectedDecalVolume.hlsl");

	if (!Program ||
		!Program->VertexShader ||
		!Program->PixelShader ||
		!Program->VertexShader->IsValid() ||
		!Program->PixelShader->IsValid())
	{
		return false;
	}

	// 셰이더 조회 실패 시 기본 셰이더가 반환되는 경우 방지.
	if (!Program->VertexShader->GetBindings().FindConstantBuffer("DecalVolumeConstants"))
	{
		return false;
	}

	if (!Program->VertexShader->GetLayout())
	{
		return false;
	}

	VolumePipelineState = DepthPipelineState;
	VolumePipelineState.Shader = Program;

	VolumePipelineState.Topology = EPrimitiveTopology::TriangleList;

	// 앞면을 제거하고 출구 면을 그린다.
	VolumePipelineState.RasterizerState = ERasterizerState::SolidFront;

	VolumeVertexBuffer =
		RenderCommand::CreateStaticVertexBuffer(
			GDecalVolumeVertices,
			static_cast<uint32>(sizeof(GDecalVolumeVertices)),
			sizeof(FDecalVolumeVertex));

	VolumeIndexBuffer =
		RenderCommand::CreateStaticIndexBuffer(
			GDecalVolumeIndices, 36);

	VolumeConstantBuffer =
		RenderCommand::CreateConstantBuffer(
			sizeof(FProjectedDecalVolumeConstants));

	return IsVolumeResourcesReady();
}

bool FProjectedDecalRenderer::IsVolumeResourcesReady() const
{
	return
		VolumePipelineState.Shader &&
		VolumePipelineState.Shader->VertexShader &&
		VolumePipelineState.Shader->PixelShader &&
		VolumeVertexBuffer &&
		VolumeVertexBuffer->GetBuffer() &&
		VolumeIndexBuffer &&
		VolumeIndexBuffer->GetBuffer() &&
		VolumeConstantBuffer &&
		VolumeConstantBuffer->GetBuffer();
}