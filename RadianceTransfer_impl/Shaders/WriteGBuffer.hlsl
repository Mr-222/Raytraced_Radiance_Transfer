#include "Common.hlsl"
#include "Util.hlsl"
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
    float3 PosW : POSITIONT;
    float3 NormalW : NORMAL;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;
    vout.PosW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.NormalW = normalize(mul(vin.NormalL, (float3x3) gWorld));
    vout.PosH = mul(float4(vout.PosW, 1.0f), gViewProj);
    
    return vout;
}

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
    gBuffer[1][pin.PosH.xy] = float4(pin.NormalW, gObjId);
}