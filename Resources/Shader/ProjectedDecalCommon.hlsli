#ifndef PROJECTED_DECAL_COMMON_HLSLI
#define PROJECTED_DECAL_COMMON_HLSLI

cbuffer DecalConstants : register(b4)
{
    matrix WorldToDecal;
    float4 TintOpacity;
    float4 TextureParams;
};

Texture2D<float4> DecalColorTexture : register(t0);
SamplerState DecalSampler : register(s0);

float4 EvaluateProjectedDecal(float3 WorldPosition)
{
    float3 DecalPosition =
        mul(
            float4(WorldPosition, 1.0f),
            WorldToDecal
        ).xyz;
    
    // 로컬 박스 밖의 픽셀 제거
    clip(0.5f - abs(DecalPosition));
    
    // 로컬 X축 투영, YZ를 UV로 사용.
    float2 UV = DecalPosition.yz + 0.5f;
    UV.y = 1.0f - UV.y; // 텍스처 좌표계는 위쪽이 0, 아래쪽이 1

    float4 TextureColor = DecalColorTexture.Sample(DecalSampler, UV);
    float3 TextureRGB = TextureColor.rgb;
    
    // 기존 UberLit의 수동 변환 규약과 맞춘다.
    // SRGB SRV는 이미 자동 변환했으므로 다시 변환하지 않는다.
    if (TextureParams.x > 0.5f)
    {
        TextureRGB = pow(max(TextureRGB, 0.0f), 2.2f);
    }
    
    float Alpha = saturate(TextureColor.a * TintOpacity.a);
    
    clip(Alpha - 0.0001f); // 알파가 0이면 픽셀 제거
    
    // 테스트 텍스처는 SRGB 포맷으로 생성한다.
    // 따라서 RGB는 샘플링할 때 이미 선형화되어 있다.
    float3 Color = TextureRGB * TintOpacity.rgb;
    
    // Premultiplied Alpha
    return float4(Color * Alpha, Alpha);
};

#endif // PROJECTED_DECAL_COMMON_HLSLI