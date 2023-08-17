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

// joint bilateral filter
void filter(float2 uv)
{
    uint width, height;
    screenSpaceFilteredHorzSHCoeffs[0].GetDimensions(width, height);
    uv.x *= width;
    uv.y *= height;
    double sumOfWeights = 0.0;
    float3 filteredColor = { 0.0f, 0.0f, 0.0f };
    
    for (int i = -radius; i <= radius; ++i)
    {
        if (uv.x + i < 0 || uv.x + i >= width)
            continue;

        if (i == 0)
        {
            sumOfWeights += 1.0;
            filteredColor += screenSpaceThisFrameSHCoeffs[0].Load(int3(uv, 0));
            continue;
        }

        float4 normalI = gBuffer[1].Load(int3(uv, 0));
        
        if (normalI.w == 0)
        {
            discard;
        }
        
        float4 normalJ = gBuffer[1].Load(int3(uv.x + i, uv.y, 0));
        float3 posI = gBuffer[0].Load(int3(uv, 0));
        float3 posJ = gBuffer[0].Load(int3(uv.x + i, uv.y, 0));
            
        if (normalJ.w == 0)
            continue;
            
        double coordTerm = calcPixelCoordTerm(uv.x, uv.y, uv.x + i, uv.y, sigmaCoord);
        double normalTerm = calcNormalTerm(normalI.xyz, normalJ.xyz, sigmaNormal);
        double planeTerm = calcPlaneTerm(normalI.xyz, posI, posJ, sigmaPlane);

        double exponent = -(coordTerm + normalTerm + planeTerm);
        double weight = exp(exponent);
        sumOfWeights += weight;
        filteredColor += screenSpaceThisFrameSHCoeffs[0].Load(int3(uv.x + i, uv.y, 0)) * weight;
    }
    
    filteredColor /= sumOfWeights;
    if (isnan(filteredColor.x))
        discard;
    
    screenSpaceFilteredHorzSHCoeffs[0][uv] = float4(filteredColor, 1.0f);
}

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;
    vout.PosH = float4(vin.PosL, 1.0f);
    vout.TexC = vin.TexC;
    return vout;
}

void PS(VertexOut pin)
{
    filter(pin.TexC);
}