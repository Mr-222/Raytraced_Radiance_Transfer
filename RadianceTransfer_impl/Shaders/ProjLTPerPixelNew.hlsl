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
    float2 TexC : TEXCOORD;
};

SHCoeff projLightTransport(float3 NormalW, float4 Visibility4, float4 RandomNumbersX, float4 RandomNumbersY)
{
    SHCoeff thisFrameSHCoeff = (SHCoeff) 0.0f;
    
    for (int i = 0; i < 4; ++i)
    {
        float visibility = Visibility4[i];
        float shEvals[9];
            
        float3 sampleVec = hemisphereSample_cos(RandomNumbersX[i], RandomNumbersY[i]);
        sampleVec = normalize(FromNormalToWorld(sampleVec, NormalW));
    
        sh_eval_basis_2(sampleVec, shEvals);
        float cosine = max(1e-5, dot(NormalW, sampleVec));
        float pdf = cosine / PI;

        thisFrameSHCoeff.SHCoeff_l0_m0 += ((visibility * cosine * shEvals[0] / pdf) / 4.0f);
        thisFrameSHCoeff.SHCoeff_l1_m_1 += ((visibility * cosine * shEvals[1] / pdf) / 4.0f);
        thisFrameSHCoeff.SHCoeff_l1_m0 += ((visibility * cosine * shEvals[2] / pdf) / 4.0f);
        thisFrameSHCoeff.SHCoeff_l1_m1 += ((visibility * cosine * shEvals[3] / pdf) / 4.0f);
        thisFrameSHCoeff.SHCoeff_l2_m_2 += ((visibility * cosine * shEvals[4] / pdf) / 4.0f);
        thisFrameSHCoeff.SHCoeff_l2_m_1 += ((visibility * cosine * shEvals[5] / pdf) / 4.0f);
        thisFrameSHCoeff.SHCoeff_l2_m0 += ((visibility * cosine * shEvals[6] / pdf) / 4.0f);
        thisFrameSHCoeff.SHCoeff_l2_m1 += ((visibility * cosine * shEvals[7] / pdf) / 4.0f);
        thisFrameSHCoeff.SHCoeff_l2_m2 += ((visibility * cosine * shEvals[8] / pdf) / 4.0f);
    }

    return thisFrameSHCoeff;
}


void calcRandomNumbers(out float4 randomNumbersX, out float4 randomNumbersY, inout RandomResult result)
{
    result = Random(result.state);
    randomNumbersX[0] = result.u;
    randomNumbersY[0] = result.v;
    
    result = Random(result.state);
    randomNumbersX[1] = result.u;
    randomNumbersY[1] = result.v;
    
    result = Random(result.state);
    randomNumbersX[2] = result.u;
    randomNumbersY[2] = result.v;
    
    result = Random(result.state);
    randomNumbersX[3] = result.u;
    randomNumbersY[3] = result.v;    
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
    float2 uv = pin.TexC;
    uint width, height;
    screenSpaceFilteredHorzSHCoeffs[0].GetDimensions(width, height);
    uv.x *= width;
    uv.y *= height;
    
    // Take this RWTexture2D as visibility4 texture.
    float4 visibility4 = screenSpaceThisFrameSHCoeffs[8][uv];
    
    float4 normalW = gBuffer[1][uv];
    if (normalW.w == 0.0f)
    {
        discard;
    }
    
    // Take this RWTexture2D as random state texture.
    uint stateIndex = uv.x + 1920 * uv.y;
    RandomResult result = gRandomState[stateIndex];
    float4 randomNumbersX = (0.0f, 0.0f, 0.0f, 0.0f);
    float4 randomNumbersY = (0.0f, 0.0f, 0.0f, 0.0f);
    calcRandomNumbers(randomNumbersX, randomNumbersY, result);
    gRandomState[stateIndex] = result;
    
    SHCoeff shCoeffsPixel = projLightTransport(normalW.xyz, visibility4, randomNumbersX, randomNumbersY);
    
    // Reconstruct light.
    SHCoeff shCoeffsLight = gSHCoeffsEnv[0];
    float3 color = (1.0f / PI) * shMultiply(shCoeffsLight, shCoeffsPixel);
    screenSpaceThisFrameSHCoeffs[0][pin.PosH.xy] = float4(color, 1.0f);
}