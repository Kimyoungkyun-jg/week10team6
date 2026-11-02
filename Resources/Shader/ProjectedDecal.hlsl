#pragma pack_matrix(row_major)

cbuffer PerObject : register(b0)
{
    matrix World;
    matrix View;
    matrix Projection;
    matrix NormalMatrix;
    float4 ShadowFlags;
}

// b3는 Forward 타일 조명이 사용?
cbuffer DecalConstants : register(b4)
{
    matrix WorldToDecal;
    float4 TintOpacity; // rgb: 색상, a: 불투명도
}

Texture2D<float4> DecalColorTexture : register(t0);
SamplerState DecalSampler : register(s0);

struct VS_IN
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
};

struct VS_OUT
{
    float4 Position : SV_POSITION;
    
    float3 WorldPosition : TEXCOORD1;
};

// 실제 데칼 패스에서는 기존 lambert vs를 재사용한다.
VS_OUT mainVS(VS_IN Input)
{
    VS_OUT Output;
    
    float4 WorldPosition = mul(float4(Input.Position, 1.0f), World);
    
    float4 ViewPosition = mul(WorldPosition, View);
    
    Output.Position = mul(ViewPosition, Projection);
    
    Output.WorldPosition = WorldPosition.xyz;
    
    return Output;
}

float4 mainPS(VS_OUT Input) : SV_Target0
{
    float3 DecalPosition =
        mul(
            float4(Input.WorldPosition, 1.0f),
            WorldToDecal
        ).xyz;
    
    // 로컬 박스 밖의 픽셀 제거
    clip(0.5f - abs(DecalPosition));
    
    // 로컬 X축 투영, YZ를 UV로 사용.
    float2 UV = DecalPosition.yz + 0.5f;
    UV.y = 1.0f - UV.y; // 텍스처 좌표계는 위쪽이 0, 아래쪽이 1

    float4 TextureColor = DecalColorTexture.Sample(DecalSampler, UV);
    
    float Alpha = saturate(TextureColor.a * TintOpacity.a);
    
    clip(Alpha - 0.0001f); // 알파가 0이면 픽셀 제거
    
    // 테스트 텍스처는 SRGB 포맷으로 생성한다.
    // 따라서 RGB는 샘플링할 때 이미 선형화되어 있다.
    float3 Color = TextureColor.rgb * TintOpacity.rgb;
    
    // Premultiplied Alpha
    return float4(Color * Alpha, Alpha);
}