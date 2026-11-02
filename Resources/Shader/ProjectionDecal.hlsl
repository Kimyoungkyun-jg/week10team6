#pragma pack_matrix(row_major)

#include "LightData.hlsli"
#include "LightEvaluation.hlsli"
#include "LightGridCommon.hlsli"

struct FAmbientLightInfo { float4 ColorIntensity; };

cbuffer DecalView : register(b0)
{
    matrix DecalWorld;
    matrix ViewProjection;
    matrix InverseViewProjection;
    matrix InverseDecalWorld;
    matrix View;
    float4 ViewportInverseSize;
};

cbuffer Lighting : register(b1)
{
    FAmbientLightInfo Ambient;
    float4 CameraPosition;
    uint4 LightCounts;
    uint4 LightOffsets;
    float4 SkyColorIntensity;
};

cbuffer DecalParams : register(b2)
{
    float4 DecalColor;
    float4 DecalOptions; // x: opacity, y: receive lighting
    float4 ShadowFlags; // x: receive shadow, y: shadow mask available
    float4 SpecularColorShininess;
};

Texture2D<float> SceneDepthTexture : register(t0);
Texture2D<float4> SceneNormalDecalMask : register(t1);
Texture2D DecalTexture : register(t2);
StructuredBuffer<FLightRenderParameters> SceneLights : register(t8);
StructuredBuffer<uint4> TileLightRanges : register(t9);
StructuredBuffer<uint> TilePointIndices : register(t10);
TextureCube<float4> SkyIrradiance : register(t11);
StructuredBuffer<uint> TileSpotIndices : register(t12);
Texture2D<float4> ScreenShadowMask : register(t13);
SamplerState ColorSampler : register(s0);
SamplerState SkySampler : register(s1);

float GetLightShadow(FLightRenderParameters Light, float3 PixelPosition)
{
    if (Light.ShadowMapChannel == INVALID_SHADOW_CHANNEL || ShadowFlags.x < 0.5f) return 1.0f;
    return ScreenShadowMask.Load(int3(int2(PixelPosition.xy), 0))[Light.ShadowMapChannel];
}

#include "ForwardLightingFunctions.hlsli"

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
};

struct VSOutput { float4 Position : SV_POSITION; };

VSOutput mainVS(VSInput Input)
{
    VSOutput Output;
    float4 WorldPosition = mul(float4(Input.Position, 1.0f), DecalWorld);
    Output.Position = mul(WorldPosition, ViewProjection);
    return Output;
}

float4 mainPS(VSOutput Input) : SV_Target
{
    int2 Pixel = int2(Input.Position.xy);
    float Depth = SceneDepthTexture.Load(int3(Pixel, 0));
    float4 NormalMask = SceneNormalDecalMask.Load(int3(Pixel, 0));
    if (Depth >= 1.0f || NormalMask.w < 0.5f) discard;

    float2 UV = Input.Position.xy * ViewportInverseSize.xy;
    float4 PositionH = mul(float4(UV.x * 2.0f - 1.0f, 1.0f - UV.y * 2.0f, Depth, 1.0f), InverseViewProjection);
    float3 WorldPosition = PositionH.xyz / PositionH.w;
    float3 LocalPosition = mul(float4(WorldPosition, 1.0f), InverseDecalWorld).xyz;
    if (any(abs(LocalPosition) > 0.5f)) discard;

    float3 Normal = normalize(NormalMask.xyz);
    float3 Forward = normalize(DecalWorld[0].xyz);
    if (dot(Normal, -Forward) < -0.1f) discard;

    float2 DecalUV = float2(LocalPosition.y + 0.5f, 0.5f - LocalPosition.z);
    float4 Sample = DecalTexture.Sample(ColorSampler, DecalUV);
    float Alpha = saturate(Sample.a * DecalColor.a * DecalOptions.x);
    if (Alpha <= 0.0f) discard;
    float3 Color = pow(max(Sample.rgb, 0.0f), 2.2f) * DecalColor.rgb;
    if (DecalOptions.y > 0.5f)
    {
        float ViewDepth = mul(float4(WorldPosition, 1.0f), View).x;
        FLightResult Lit = CalculateTiledLighting(Input.Position.xyz, WorldPosition, ViewDepth, Normal, true);
        Color = Color * Lit.Diffuse + Lit.Specular;
    }
    else if (ShadowFlags.x > 0.5f && ShadowFlags.y > 0.5f)
    {
        float4 Shadow = ScreenShadowMask.Load(int3(Pixel, 0));
        Color *= min(min(Shadow.x, Shadow.y), min(Shadow.z, Shadow.w));
    }
    return float4(Color, Alpha);
}
