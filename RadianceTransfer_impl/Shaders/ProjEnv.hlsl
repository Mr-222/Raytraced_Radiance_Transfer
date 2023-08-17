#include "Common.hlsl"
#include "Sample.hlsl"
 
static const uint ordersNum = 9; // 9 basis functions

void VS()
{
    SHCoeff envCoeffs = (SHCoeff) 0.0f;
    static const uint sampleNum = 2000;
    const float pdf = 1 / (4 * PI);
    
    for (uint i = 0; i < sampleNum / 2; ++i)
    {
        float shEvals[9];
        float2 uv = hammersley2d(i, sampleNum/2);
        float3 sampleVec = hemisphereSample_uniform(uv.x, uv.y);
        sh_eval_basis_2(normalize(sampleVec), shEvals);
        float3 L = gCubeMap.SampleLevel(gsamPointWrap, sampleVec, 0);
        
        envCoeffs.SHCoeff_l0_m0 += L * shEvals[0];
        envCoeffs.SHCoeff_l1_m_1 += L * shEvals[1];
        envCoeffs.SHCoeff_l1_m0 += L * shEvals[2];
        envCoeffs.SHCoeff_l1_m1 += L * shEvals[3];
        envCoeffs.SHCoeff_l2_m_2 += L * shEvals[4];
        envCoeffs.SHCoeff_l2_m_1 += L * shEvals[5];
        envCoeffs.SHCoeff_l2_m0 += L * shEvals[6];
        envCoeffs.SHCoeff_l2_m1 += L * shEvals[7];
        envCoeffs.SHCoeff_l2_m2 += L * shEvals[8];
    }
    
    for (uint j = 0; j < sampleNum / 2; ++j)
    {
        float shEvals[9];
        float2 uv = hammersley2d(j, sampleNum/2);
        float3 sampleVec = hemisphereSample_uniform(uv.x, uv.y);
        sampleVec.z = -sampleVec.z;
        sh_eval_basis_2(normalize(sampleVec), shEvals);
        float3 L = gCubeMap.SampleLevel(gsamPointWrap, sampleVec, 0);
        //float3 L = float3(1.0f, 1.0f, 1.0f);
        
        envCoeffs.SHCoeff_l0_m0 += L * shEvals[0];
        envCoeffs.SHCoeff_l1_m_1 += L * shEvals[1];
        envCoeffs.SHCoeff_l1_m0 += L * shEvals[2];
        envCoeffs.SHCoeff_l1_m1 += L * shEvals[3];
        envCoeffs.SHCoeff_l2_m_2 += L * shEvals[4];
        envCoeffs.SHCoeff_l2_m_1 += L * shEvals[5];
        envCoeffs.SHCoeff_l2_m0 += L * shEvals[6];
        envCoeffs.SHCoeff_l2_m1 += L * shEvals[7];
        envCoeffs.SHCoeff_l2_m2 += L * shEvals[8];
    }
    
    envCoeffs.SHCoeff_l0_m0 /= (sampleNum * pdf);
    envCoeffs.SHCoeff_l1_m_1 /= (sampleNum * pdf);
    envCoeffs.SHCoeff_l1_m0 /= (sampleNum * pdf);
    envCoeffs.SHCoeff_l1_m1 /= (sampleNum * pdf);
    envCoeffs.SHCoeff_l2_m_2 /= (sampleNum * pdf);
    envCoeffs.SHCoeff_l2_m_1 /= (sampleNum * pdf);
    envCoeffs.SHCoeff_l2_m0 /= (sampleNum * pdf);
    envCoeffs.SHCoeff_l2_m1 /= (sampleNum * pdf);
    envCoeffs.SHCoeff_l2_m2 /= (sampleNum * pdf);
    
    gSHCoeffsEnv[0] = envCoeffs;
}