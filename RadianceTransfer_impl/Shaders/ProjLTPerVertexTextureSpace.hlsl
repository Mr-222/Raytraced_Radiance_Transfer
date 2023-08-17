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

void projLightTransport(VertexIn vin, uint vid)
{
    float4 visibility4 = textureSpaceVisibility4.SampleLevel(gsamPointClamp, vin.TexC, 0);
    //uint height, width;
    //textureSpaceVisibility4.GetDimensions(width, height);
    //float4 visibility4 = textureSpaceVisibility4.Load(int3(width*vin.TexC.x, height*vin.TexC.y, 0));
    
    SHCoeff thisFrameSHCoeff = (SHCoeff) 0.0f;
    RandomResult result = gRandomState[vid];
    
    for (int i = 0; i < 4; ++i)
    {
        float visibility = visibility4[i];
        
        // Transform to world space.
        float3 posW = mul(float4(vin.PosL, 1.0f), gWorld);

        // Assumes nonuniform scaling; otherwise, need to use inverse-transpose of world matrix.
        float3 NormalW = normalize(mul(vin.NormalL, (float3x3) gWorld));
        
        float shEvals[9];
        
        result = Random(result.state);
        float3 sampleVec = hemisphereSample_cos(result.u, result.v);
    
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
    
    gRandomState[vid] = result;
    gThisFrameSHCoeffsObject[vid] = thisFrameSHCoeff;
}

void VS(VertexIn vin, uint vid : SV_VertexID)
{
    projLightTransport(vin, vid);
}
