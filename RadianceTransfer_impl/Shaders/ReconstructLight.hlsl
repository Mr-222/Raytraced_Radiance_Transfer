#include "Common.hlsl"
#include "Sample.hlsl"

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float3 TangentU : TANGENT;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 Albedo : COLOR0;
    float3 PosW : POSITIONT;
    float3 NormalW : NORMAL;
    SHCoeff shCoeffsVertex : SH_COEFF;
};

VertexOut VS(VertexIn vin, uint vid : SV_VertexID)
{   
    vid = vid + gVertexOffset;
    VertexOut vout = (VertexOut) 0.0f;

    float3 albedo = float3(1.0f, 1.0f, 1.0f);
    vout.Albedo = albedo;

    float ratio = 0.9;
    vout.shCoeffsVertex =  shBlend(ratio, gTemporalSHCoeffsObject[vid], gThisFrameSHCoeffsObject[vid]);
    gTemporalSHCoeffsObject[vid] = vout.shCoeffsVertex;
    
    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    vout.NormalW = normalize(mul(vin.NormalL, (float3x3) gWorld));
    vout.PosH = mul(posW, gViewProj);
    
    return vout;
}

[earlydepthstencil]
void PS(VertexOut pin)
{
    // Compare the depth value, if this pixel's depth bigger than the depth in the z-buffer
    // this pixel won't have any effect on display
    float width, height;
    gDepthMap.GetDimensions(width, height);
    float trueDepth = gDepthMap.Sample(gsamLinearClamp, float2(pin.PosH.x / width, pin.PosH.y / height));
    if (pin.PosH.z > trueDepth - 0.0059) // Somehow previously generated depth(depth map) has a offset(approximately 0.6) compared to the current depth
    {
        discard;
    }
    
    // Write gBuffer.
    gBuffer[0][pin.PosH.xy] = float4(pin.PosW, 1.0f);
    gBuffer[1][pin.PosH.xy] = float4(pin.NormalW, 1.0f);
    
    SHCoeff shCoeffsLight = gSHCoeffsEnv[0];
    SHCoeff shCoeffsVertex = pin.shCoeffsVertex;
    
    float3 color = (pin.Albedo / PI) * shMultiply(shCoeffsLight, shCoeffsVertex);
    
    screenSpaceThisFrameSHCoeffs[0][pin.PosH.xy] = float4(color, 1.0f);
}