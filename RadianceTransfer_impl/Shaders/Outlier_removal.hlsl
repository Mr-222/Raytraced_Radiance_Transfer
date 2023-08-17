#include "Common.hlsl"
#include "FilterUtil.hlsl"

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
    float2 TexC : TEXCOORD;
};

void outlierRemoval(float2 uv, float3 pixelColor)
{
    float3 mean = float3(0.0f, 0.0f, 0.0f);
    float variance = 0.0f;
    int r = 3;
    for (int i = -r; i <= r; ++i)
    {
        for (int j = -r; j <= r; ++j)
        {
            mean += screenSpaceThisFrameSHCoeffs[0][uint2(uv.x + i, uv.y + j)].xyz;
        }
    }
    mean /= 49.0f;
        
    for (int i = -r; i <= r; ++i)
    {
        for (int j = -r; j <= r; ++j)
        {
            variance += pow(distance(mean, screenSpaceThisFrameSHCoeffs[0][uint2(uv.x + i, uv.y + j)].xyz), 2);
        }
    }
    variance /= 48.0f;
    variance = sqrt(variance);
    pixelColor = clamp(pixelColor.xyz, mean - (variance * sigmaOutlierRemoval), mean + (variance * sigmaOutlierRemoval));
    
    screenSpaceThisFrameSHCoeffs[1][uv] = float4(pixelColor, 1.0f);
}

VertexOut
    VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;
    vout.PosH = float4(vin.PosL, 1.0f);
    vout.TexC = vin.TexC;
    return vout;
}

void PS(VertexOut pin)
{    
    float2 uv = pin.TexC;
    uint width, height;
    screenSpaceFilteredHorzSHCoeffs[0].GetDimensions(width, height);
    uv.x *= width;
    uv.y *= height;
    
    if (gBuffer[1][uv].w == 0.0f)
        discard;

    float3 color = screenSpaceThisFrameSHCoeffs[0][uv].xyz;
    outlierRemoval(uv, color);
}