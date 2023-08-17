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
    float3 LastFrameNDCPos : POSITIONT1;
    float3 Albedo : COLOR;
};

float3 reconstructLight(SHCoeff shCoeffsLight, SHCoeff thisFrameSHCoeff, float3 albedo, uint2 uv, float2 uvFromLastFrame, bool offScreen)
{
    SHCoeff shCoeffsObject = (SHCoeff) 0.0f;
    SHCoeff lastFrameSHCoeff = (SHCoeff) 0.0f;
    float ratio = 0.0;
    
    if (!offScreen)
    {
        ratio = 0.9;
        lastFrameSHCoeff = shLoad(uvFromLastFrame, screenSpaceLastFrameSHCoeffs);
    }
    
    // temporal filtering
    shCoeffsObject = shBlend(ratio, lastFrameSHCoeff, thisFrameSHCoeff);
     
    float3 color = (albedo / PI) * shMultiply(shCoeffsLight, shCoeffsObject);
    
    // update shCoeffs
    shSave(shCoeffsObject, uv, screenSpaceIntermediateSHCoeffs);
    
    return color;
}

VertexOut VS(VertexIn vin, uint vid : SV_VertexID)
{
    VertexOut vout = (VertexOut) 0.0f;
    float3 PosW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosH = mul(float4(PosW, 1.0f), gViewProj);
    vout.Albedo = float3(1.0f, 1.0f, 1.0f);
    
    float4 lastFrameNdcPos = mul(float4(vin.PosL, 1.0f), gLastFrameWorld);
    lastFrameNdcPos = mul(lastFrameNdcPos, gLastFrameViewProj);
    
    // Convert to NDC space.
    lastFrameNdcPos /= lastFrameNdcPos.w;
    vout.LastFrameNDCPos = lastFrameNdcPos.xyz;
    
    return vout;
}

[earlydepthstencil]
float4 PS(VertexOut pin) : SV_Target
{
    float width, height;
    gDepthMap.GetDimensions(width, height);
    float2 uv = pin.PosH.xy;
    SHCoeff shCoeffsPixel = shLoad(uv, screenSpaceFilteredVertSHCoeffs);
    
    SHCoeff shCoeffsLight = gSHCoeffsEnv[0];
    
    // judge if the pixel is off-screen in the last frame
    bool offScreen = (pin.LastFrameNDCPos.x < -1 || pin.LastFrameNDCPos.x > 1 ||
                                 pin.LastFrameNDCPos.y < -1 || pin.LastFrameNDCPos.y > 1 ||
                                 pin.LastFrameNDCPos.z < 0 || pin.LastFrameNDCPos.z > 1);
    // Convert from [-1, 1] to [0, 1].
    screenSpaceIntermediateSHCoeffs[0].GetDimensions(width, height);
    // Viewport's origin is defined at top left corner, so inverse y.
    pin.LastFrameNDCPos.y = -pin.LastFrameNDCPos.y;
    // Convert from [-1, 1] to [0, 1].
    float2 LastFrameUV = (0.5 * pin.LastFrameNDCPos.xy) + 0.5;
    LastFrameUV *= float2(width, height);
    float3 color = reconstructLight(shCoeffsLight, shCoeffsPixel, pin.Albedo, pin.PosH.xy, LastFrameUV, offScreen);

    return float4(color, 1.0f);
}