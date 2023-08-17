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
    float4x4 visibility4x4 : VISIBILITY;
    float4x4 randomNumbersX : U;
    float4x4 randomNumbersY : V;
};

SHCoeff projLightTransport(float3 NormalW, float4x4 Visibility4x4, float4x4 RandomNumbersX, float4x4 RandomNumbersY)
{
    SHCoeff thisFrameSHCoeff = (SHCoeff) 0.0f;
    
    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            float visibility = Visibility4x4[i][j];
            float shEvals[9];
            
            float3 sampleVec = hemisphereSample_cos(RandomNumbersX[i][j], RandomNumbersY[i][j]);
            sampleVec = normalize(FromNormalToWorld(sampleVec, NormalW));
    
            sh_eval_basis_2(sampleVec, shEvals);
            float cosine = max(1e-5, dot(NormalW, sampleVec));
            float pdf = cosine / PI;

            thisFrameSHCoeff.SHCoeff_l0_m0 += ((visibility * cosine * shEvals[0] / pdf) / 16.0f);
            thisFrameSHCoeff.SHCoeff_l1_m_1 += ((visibility * cosine * shEvals[1] / pdf) / 16.0f);
            thisFrameSHCoeff.SHCoeff_l1_m0 += ((visibility * cosine * shEvals[2] / pdf) / 16.0f);
            thisFrameSHCoeff.SHCoeff_l1_m1 += ((visibility * cosine * shEvals[3] / pdf) / 16.0f);
            thisFrameSHCoeff.SHCoeff_l2_m_2 += ((visibility * cosine * shEvals[4] / pdf) / 16.0f);
            thisFrameSHCoeff.SHCoeff_l2_m_1 += ((visibility * cosine * shEvals[5] / pdf) / 16.0f);
            thisFrameSHCoeff.SHCoeff_l2_m0 += ((visibility * cosine * shEvals[6] / pdf) / 16.0f);
            thisFrameSHCoeff.SHCoeff_l2_m1 += ((visibility * cosine * shEvals[7] / pdf) / 16.0f);
            thisFrameSHCoeff.SHCoeff_l2_m2 += ((visibility * cosine * shEvals[8] / pdf) / 16.0f);
        }
    }

    return thisFrameSHCoeff;
}


void calcRandomNumbers(out float4x4 randomNumbersX, out float4x4 randomNumbersY, inout RandomResult result)
{
    result = Random(result.state);
    randomNumbersX._m00 = result.u;
    randomNumbersY._m00 = result.v;
    
    result = Random(result.state);
    randomNumbersX._m01 = result.u;
    randomNumbersY._m01 = result.v;
    
    result = Random(result.state);
    randomNumbersX._m02 = result.u;
    randomNumbersY._m02 = result.v;
    
    result = Random(result.state);
    randomNumbersX._m03 = result.u;
    randomNumbersY._m03 = result.v;
    
    result = Random(result.state);
    randomNumbersX._m10 = result.u;
    randomNumbersY._m10 = result.v;
    
    result = Random(result.state);
    randomNumbersX._m11 = result.u;
    randomNumbersY._m11 = result.v;
    
    result = Random(result.state);
    randomNumbersX._m12 = result.u;
    randomNumbersY._m12 = result.v;
    
    result = Random(result.state);
    randomNumbersX._m13 = result.u;
    randomNumbersY._m13 = result.v;
    
    result = Random(result.state);
    randomNumbersX._m20 = result.u;
    randomNumbersY._m20 = result.v;
    
    result = Random(result.state);
    randomNumbersX._m21 = result.u;
    randomNumbersY._m21 = result.v;
    
    result = Random(result.state);
    randomNumbersX._m22 = result.u;
    randomNumbersY._m22 = result.v;
    
    result = Random(result.state);
    randomNumbersX._m23 = result.u;
    randomNumbersY._m23 = result.v;
    
    result = Random(result.state);
    randomNumbersX._m30 = result.u;
    randomNumbersY._m30 = result.v;
    
    result = Random(result.state);
    randomNumbersX._m31 = result.u;
    randomNumbersY._m31 = result.v;
    
    result = Random(result.state);
    randomNumbersX._m32 = result.u;
    randomNumbersY._m32 = result.v;
    
    result = Random(result.state);
    randomNumbersX._m33 = result.u;
    randomNumbersY._m33 = result.v;
}

VertexOut VS(VertexIn vin, uint vid : SV_VertexID)
{
    VertexOut vout = (VertexOut) 0.0f;
    vout.PosW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.NormalW = normalize(mul(vin.NormalL, (float3x3) gWorld));
    vout.PosH = mul(float4(vout.PosW, 1.0f), gViewProj);
    vout.visibility4x4 = gVisibility4x4[vid];
    
    RandomResult result = gRandomState[vid];
    float4x4 randomNumberX;
    float4x4 randomNumberY;
    calcRandomNumbers(randomNumberX, randomNumberY, result);
    
    vout.randomNumbersX = randomNumberX;
    vout.randomNumbersY = randomNumberY;
    
    gRandomState[vid] = result;
    
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
    
    SHCoeff shCoeffsPixel = projLightTransport(normalize(pin.NormalW), pin.visibility4x4, pin.randomNumbersX, pin.randomNumbersY);
    
    // Reconstruct light.
    SHCoeff shCoeffsLight = gSHCoeffsEnv[0];
    float3 color = (1.0f / PI) * shMultiply(shCoeffsLight, shCoeffsPixel);
    screenSpaceThisFrameSHCoeffs[0][pin.PosH.xy] = float4(color, 1.0f);
}