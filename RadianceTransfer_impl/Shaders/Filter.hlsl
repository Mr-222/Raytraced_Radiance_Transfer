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
};

// joint bilateral filter
void filter(float2 uv)
{
    uint width, height;
    gBuffer[0].GetDimensions(width, height);
    double sumOfWeights = 0.0;
    SHCoeff filteredCoeffs = (SHCoeff) 0.0f;
    for(int i=-radius; i<=radius; ++i)
    {
        if(uv.x+i < 0 || uv.x+i >= width)
            continue;

        for(int j=-radius; j<=radius; ++j)
        {
            if(uv.y+j < 0 || uv.y+j >= height)
                continue;

            if(i==0 && j==0)
            {
                sumOfWeights += 1.0;
                filteredCoeffs = shAdd(filteredCoeffs, shLoad(uv, screenSpaceThisFrameSHCoeffs));
                continue;
            }

            float3 normalI = gBuffer[1].Load(int3(uv, 0));
            float3 normalJ = gBuffer[1].Load(int3(uv.x+i, uv.y+j, 0));
            float3 posI = gBuffer[0].Load(int3(uv, 0));
            float3 posJ = gBuffer[0].Load(int3(uv.x+i, uv.y+j, 0));
            
            if (length(normalJ) == 0)
                continue;
            
            double coordTerm = calcPixelCoordTerm(uv.x, uv.y, uv.x+i, uv.y+j, sigmaCoord);
            double normalTerm = calcNormalTerm(normalI, normalJ, sigmaNormal);
            double planeTerm = calcPlaneTerm(normalI, posI, posJ, sigmaPlane);

            double exponent = -(coordTerm + normalTerm + planeTerm);
            double weight = exp(exponent);
            sumOfWeights += weight;
            filteredCoeffs = shAdd(filteredCoeffs, shLoad(float2(uv.x + i, uv.y + j), screenSpaceThisFrameSHCoeffs), weight);
        }
    }
    filteredCoeffs = shMultiply(filteredCoeffs, 1 / sumOfWeights);
    shSave(filteredCoeffs, uv, screenSpaceFilteredVertSHCoeffs);
}

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;
    float4 PosW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosH = mul(PosW, gViewProj);
    return vout;
}

[earlydepthstencil]
void PS(VertexOut pin)
{
    filter(pin.PosH.xy);
}