#pragma pack_matrix(row_major)

#include "ProjectedDecalCommon.hlsli"

cbuffer DecalDepthViewConstants : register(b5)
{
    matrix InverseViewProjection;
    
    float4 ViewportSize;
};

// t0는 데칼 색상 텍스처.
Texture2D<float> SceneDepthTexture : register(t1);

struct VS_OUT
{
    float4 Position : SV_Position;
};

// 버텍스 버퍼 없이 전체 화면 삼각형 생성.
// TODO: 데칼 볼륨의 버텍스 버퍼를 사용
VS_OUT mainVS(uint VertexID : SV_VertexID)
{
    VS_OUT Output;

    float2 UV =
        float2(
            (VertexID << 1) & 2,
            VertexID & 2);

    Output.Position = float4(
        UV * float2(2.0f, -2.0f) +
            float2(-1.0f, 1.0f),
        0.0f,
        1.0f);

    return Output;
}

float4 mainPS(VS_OUT Input) : SV_Target0
{
    int2 Pixel = int2(Input.Position.xy);
    
    float Depth =
    SceneDepthTexture.Load(int3(Pixel, 0));
    
    // 현재 엔진의 깊이 Clear 값은 1.
    // 배경은 데칼을 받지 않는다.
    if (Depth >= 1.0f)
        discard;
    
    // SV_POSITION은 이미 픽셀 중심 좌표.
    // 여기서 0.5를 다시 더하지 않는다.
    float2 ScreenUV =
        Input.Position.xy * ViewportSize.zw;
    
    // 화면 좌표 -> D3D NDC.
    // 현재 엔진의 깊이는 [0, 1]이므로 z는 그대로 사용한다.
    float4 ClipPosition = float4(
        ScreenUV.x * 2.0f - 1.0f,
        1.0f - ScreenUV.y * 2.0f,
        Depth,
        1.0f);

    float4 WorldPositionH =
        mul(ClipPosition, InverseViewProjection);
    
    if (abs(WorldPositionH.w) < 1.0e-8f)
        discard;
    
    float3 WorldPosition = WorldPositionH.xyz / WorldPositionH.w;
    
    return EvaluateProjectedDecal(WorldPosition);

}