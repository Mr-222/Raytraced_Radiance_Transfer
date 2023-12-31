//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define SH_MAX_COEFF (SH_MAX_ORDER * SH_MAX_ORDER)
#define SH_EVAL_BASIS(n, d, dir, result) float r[(n * n)]; sh_eval_basis_##d(dir, r); [unroll] for (uint i = 0; i < (n * n); ++i) result[i] = r[i]
#define CASE_SH_EVAL_BASIS(order, degree) case order: { SH_EVAL_BASIS(order, degree, dir, result); break; }

// Order0~Order2
struct SHCoeff
{
    float3 SHCoeff_l0_m0;
    float3 SHCoeff_l1_m_1;
    float3 SHCoeff_l1_m0;
    float3 SHCoeff_l1_m1;
    float3 SHCoeff_l2_m_2;
    float3 SHCoeff_l2_m_1;
    float3 SHCoeff_l2_m0;
    float3 SHCoeff_l2_m1;
    float3 SHCoeff_l2_m2;
};

// routine generated programmatically for evaluating SH basis for degree 1
// inputs (x, y, z) are a point on the sphere (i.e., must be unit length)
// output is vector b with SH basis evaluated at (x, y, z).
void sh_eval_basis_1(float3 v, out float b[4])
{
	// m = 0 //
	// l = 0
	const float p_0_0 = 0.282094791773878140;
	b[0] = p_0_0; // l = 0, m = 0
	// l = 1
	const float p_1_0 = 0.488602511902919920 * v.z;
	b[2] = p_1_0; // l = 1, m = 0

	// m = 1 //
	const float s1 = v.y;
	const float c1 = v.x;

	// l = 1
	const float p_1_1 = -0.488602511902919920;
	b[1] = p_1_1 * s1; // l = 1, m = -1
	b[3] = p_1_1 * c1; // l = 1, m = +1
}

// routine generated programmatically for evaluating SH basis for degree 2
// inputs (x, y, z) are a point on the sphere (i.e., must be unit length)
// output is vector b with SH basis evaluated at (x, y, z).
void sh_eval_basis_2(float3 v, out float b[9])
{
	// Reuse sh_eval_basis_1()
	float r[4];
	sh_eval_basis_1(v, r);
	[unroll]
	for (uint i = 0; i < 4; ++i)
		b[i] = r[i];

	const float z2 = v.z * v.z;

	// m = 0 //
	// l = 2
	const float p_2_0 = 0.946174695757560080 * z2 - 0.315391565252520050;
	b[6] = p_2_0; // l = 2, m = 0

	// m = 1 //
	const float s1 = v.y;
	const float c1 = v.x;
	// l = 2
	const float p_2_1 = -1.092548430592079200 * v.z;
	b[5] = p_2_1 * s1; // l = 2, m = -1
	b[7] = p_2_1 * c1; // l = 2, m = +1

	// m = 2 //
	const float s2 = v.x * s1 + v.y * c1;
	const float c2 = v.x * c1 - v.y * s1;
	// l = 2
	const float p_2_2 = 0.546274215296039590;
	b[4] = p_2_2 * s2; // l = 2, m = -2
	b[8] = p_2_2 * c2; // l = 2, m = +2
}

// routine generated programmatically for evaluating SH basis for degree 3
// inputs (x, y, z) are a point on the sphere (i.e., must be unit length)
// output is vector b with SH basis evaluated at (x, y, z).
void sh_eval_basis_3(float3 v, out float b[16])
{
	// Reuse sh_eval_basis_2()
	float r[9];
	sh_eval_basis_2(v, r);
	[unroll]
	for (uint i = 0; i < 9; ++i)
		b[i] = r[i];

	const float z2 = v.z * v.z;

	// m = 0 //
	// l = 3
	const float p_3_0 = v.z * (1.865881662950577000 * z2 - 1.119528997770346200);
	b[12] = p_3_0; // l = 3, m = 0

	// m = 1 //
	const float s1 = v.y;
	const float c1 = v.x;
	// l = 3
	const float p_3_1 = -2.285228997322328800 * z2 + 0.457045799464465770;
	b[11] = p_3_1 * s1; // l = 3, m = -1
	b[13] = p_3_1 * c1; // l = 3, m = +1

	// m = 2 //
	const float s2 = v.x * s1 + v.y * c1;
	const float c2 = v.x * c1 - v.y * s1;
	// l = 3
	const float p_3_2 = 1.445305721320277100 * v.z;
	b[10] = p_3_2 * s2; // l = 3, m =- 2
	b[14] = p_3_2 * c2; // l = 3, m =+ 2

	// m = 3 //
	const float s3 = v.x * s2 + v.y * c2;
	const float c3 = v.x * c2 - v.y * s2;
	// l = 3
	const float p_3_3 = -0.590043589926643520;
	b[9] = p_3_3 * s3; // l = 3, m = -3
	b[15] = p_3_3 * c3; // l = 3, m = +3
}

// routine generated programmatically for evaluating SH basis for degree 4
// inputs (x, y, z) are a point on the sphere (i.e., must be unit length)
// output is vector b with SH basis evaluated at (x, y, z).
void sh_eval_basis_4(float3 v, out float b[25])
{
	// Reuse sh_eval_basis_3()
	float r[16];
	sh_eval_basis_3(v, r);
	[unroll]
	for (uint i = 0; i < 16; ++i)
		b[i] = r[i];

	const float z2 = v.z * v.z;

	// m = 0 //
	// l = 4
	const float p_2_0 = 0.946174695757560080 * z2 - 0.315391565252520050;
	const float p_3_0 = v.z * (1.865881662950577000 * z2 - 1.119528997770346200);
	const float p_4_0 = 1.984313483298443000 * v.z * p_3_0 - 1.006230589874905300 * p_2_0;
	b[20] = p_4_0; // l = 4, m = 0

	// m = 1 //
	const float s1 = v.y;
	const float c1 = v.x;
	// l = 4
	const float p_4_1 = v.z * (-4.683325804901024000 * z2 + 2.007139630671867200);
	b[19] = p_4_1 * s1; // l = 4, m = -1
	b[21] = p_4_1 * c1; // l = 4, m = +1

	// m = 2 //
	const float s2 = v.x * s1 + v.y * c1;
	const float c2 = v.x * c1 - v.y * s1;
	// l = 4
	const float p_4_2 = 3.311611435151459800 * z2 - 0.473087347878779980;
	b[18] = p_4_2 * s2; // l = 4, m = -2
	b[22] = p_4_2 * c2; // l = 4, m = +2

	// m = 3 //
	const float s3 = v.x * s2 + v.y * c2;
	const float c3 = v.x * c2 - v.y * s2;
	// l = 4
	const float p_4_3 = -1.770130769779930200 * v.z;
	b[17] = p_4_3 * s3; // l = 4, m = -3
	b[23] = p_4_3 * c3; // l = 4, m = +3

	// m = 4 //
	const float s4 = v.x * s3 + v.y * c3;
	const float c4 = v.x * c3 - v.y * s3;
	// l = 4
	const float p_4_4 = 0.625835735449176030;
	b[16] = p_4_4 * s4; // l = 4, m= -4
	b[24] = p_4_4 * c4; // l = 4, m= +4
}

// routine generated programmatically for evaluating SH basis for degree 5
// inputs (x, y, z) are a point on the sphere (i.e., must be unit length)
// output is vector b with SH basis evaluated at (x, y, z).
void sh_eval_basis_5(float3 v, out float b[36])
{
	// Reuse sh_eval_basis_4()
	float r[25];
	sh_eval_basis_4(v, r);
	[unroll]
	for (uint i = 0; i < 25; ++i)
		b[i] = r[i];

	const float z2 = v.z * v.z;

	// m = 0 //
	// l = 5
	const float p_2_0 = 0.946174695757560080 * z2 - 0.315391565252520050;
	const float p_3_0 = v.z * (1.865881662950577000 * z2 - 1.119528997770346200);
	const float p_4_0 = 1.984313483298443000 * v.z * p_3_0 - 1.006230589874905300 * p_2_0;
	const float p_5_0 = 1.989974874213239700 * v.z * p_4_0 - 1.002853072844814000 * p_3_0;
	b[30] = p_5_0; // l = 5, m = 0

	// m = 1 //
	const float s1 = v.y;
	const float c1 = v.x;
	// l = 5
	const float p_3_1 = -2.285228997322328800 * z2 + 0.457045799464465770;
	const float p_4_1 = v.z * (-4.683325804901024000 * z2 + 2.007139630671867200);
	const float p_5_1 = 2.031009601158990200 * v.z * p_4_1 - 0.991031208965114650 * p_3_1;
	b[29] = p_5_1 * s1; // l = 5, m= -1
	b[31] = p_5_1 * c1; // l = 5, m= +1

	// m = 2 //
	const float s2 = v.x * s1 + v.y * c1;
	const float c2 = v.x * c1 - v.y * s1;
	// l = 5
	const float p_5_2 = v.z * (7.190305177459987500 * z2 - 2.396768392486662100);
	b[28] = p_5_2 * s2; // l = 5, m = -2
	b[32] = p_5_2 * c2; // l = 5, m = +2

	// m = 3 //
	const float s3 = v.x * s2 + v.y * c2;
	const float c3 = v.x * c2 - v.y * s2;
	// l = 5
	const float p_5_3 = -4.403144694917253700 * z2 + 0.489238299435250430;
	b[27] = p_5_3 * s3; // l = 5, m = -3
	b[33] = p_5_3 * c3; // l = 5, m = +3

	// m = 4 //
	const float s4 = v.x * s3 + v.y * c3;
	const float c4 = v.x * c3 - v.y * s3;
	// l = 5
	const float p_5_4 = 2.075662314881041100 * v.z;
	b[26] = p_5_4 * s4; // l = 5, m = -4
	b[34] = p_5_4 * c4; // l = 5, m = +4

	// m = 5 //
	const float s5 = v.x * s4 + v.y * c4;
	const float c5 = v.x * c4 - v.y * s4;
	// l = 5
	const float p_5_5 = -0.656382056840170150;
	b[25] = p_5_5 * s5; // l = 5, m = -5
	b[35] = p_5_5 * c5; // l = 5, m = +5
}

SHCoeff shAdd(SHCoeff Summand, SHCoeff Addend, float ratio = 1.0f)
{
    SHCoeff shCoeffs = (SHCoeff) 0.0f;
    shCoeffs.SHCoeff_l0_m0 = Summand.SHCoeff_l0_m0 + (Addend.SHCoeff_l0_m0 * ratio);
    shCoeffs.SHCoeff_l1_m_1 = Summand.SHCoeff_l1_m_1 + (Addend.SHCoeff_l1_m_1 * ratio);
    shCoeffs.SHCoeff_l1_m0 = Summand.SHCoeff_l1_m0 + (Addend.SHCoeff_l1_m0 * ratio);
    shCoeffs.SHCoeff_l1_m1 = Summand.SHCoeff_l1_m1 + (Addend.SHCoeff_l1_m1 * ratio);
    shCoeffs.SHCoeff_l2_m_2 = Summand.SHCoeff_l2_m_2 + (Addend.SHCoeff_l2_m_2 * ratio);
    shCoeffs.SHCoeff_l2_m_1 = Summand.SHCoeff_l2_m_1 + (Addend.SHCoeff_l2_m_1 * ratio);
    shCoeffs.SHCoeff_l2_m0 = Summand.SHCoeff_l2_m0 + (Addend.SHCoeff_l2_m0 * ratio);
    shCoeffs.SHCoeff_l2_m1 = Summand.SHCoeff_l2_m1 + (Addend.SHCoeff_l2_m1 * ratio);
    shCoeffs.SHCoeff_l2_m2 = Summand.SHCoeff_l2_m2 + (Addend.SHCoeff_l2_m2 * ratio);
    return shCoeffs;
}

void shAssign(out SHCoeff Lhs, in SHCoeff Rhs)
{
	Lhs.SHCoeff_l0_m0 = Rhs.SHCoeff_l0_m0;
	Lhs.SHCoeff_l1_m_1 = Rhs.SHCoeff_l1_m_1;
	Lhs.SHCoeff_l1_m0 = Rhs.SHCoeff_l1_m0;
	Lhs.SHCoeff_l1_m1 = Rhs.SHCoeff_l1_m1;
	Lhs.SHCoeff_l2_m_2 = Rhs.SHCoeff_l2_m_2;
	Lhs.SHCoeff_l2_m_1 = Rhs.SHCoeff_l2_m_1;
	Lhs.SHCoeff_l2_m0 = Rhs.SHCoeff_l2_m0;
	Lhs.SHCoeff_l2_m1 = Rhs.SHCoeff_l2_m1;
	Lhs.SHCoeff_l2_m2 = Rhs.SHCoeff_l2_m2;
}

SHCoeff shLoad(float2 uv, RWTexture2D<float4> textures[9])
{
    SHCoeff result = (SHCoeff) 0.0f;
    result.SHCoeff_l0_m0 = textures[0].Load(int3(uv, 0));
	result.SHCoeff_l1_m_1 = textures[1].Load(int3(uv, 0));
	result.SHCoeff_l1_m0 = textures[2].Load(int3(uv, 0));
	result.SHCoeff_l1_m1 = textures[3].Load(int3(uv, 0));
	result.SHCoeff_l2_m_2 = textures[4].Load(int3(uv, 0));
	result.SHCoeff_l2_m_1 = textures[5].Load(int3(uv, 0));
	result.SHCoeff_l2_m0 = textures[6].Load(int3(uv, 0));
	result.SHCoeff_l2_m1 = textures[7].Load(int3(uv, 0));
	result.SHCoeff_l2_m2 = textures[8].Load(int3(uv, 0));

	return result;
}

void shSave(SHCoeff shCoeff, float2 uv, RWTexture2D<float4> textures[9])
{
    textures[0][uv] = float4(shCoeff.SHCoeff_l0_m0, 1.0f);
    textures[1][uv] = float4(shCoeff.SHCoeff_l1_m_1, 1.0f);
    textures[2][uv] = float4(shCoeff.SHCoeff_l1_m0, 1.0f);
    textures[3][uv] = float4(shCoeff.SHCoeff_l1_m1, 1.0f);
    textures[4][uv] = float4(shCoeff.SHCoeff_l2_m_2, 1.0f);
    textures[5][uv] = float4(shCoeff.SHCoeff_l2_m_1, 1.0f);
    textures[6][uv] = float4(shCoeff.SHCoeff_l2_m0, 1.0f);
    textures[7][uv] = float4(shCoeff.SHCoeff_l2_m1, 1.0f);
    textures[8][uv] = float4(shCoeff.SHCoeff_l2_m2, 1.0f);
}

SHCoeff shMultiply(SHCoeff multiplicand, float ratio)
{
    SHCoeff result = (SHCoeff)0.0f;
	
	result.SHCoeff_l0_m0 =  multiplicand.SHCoeff_l0_m0 * ratio;
    result.SHCoeff_l1_m_1 = multiplicand.SHCoeff_l1_m_1 * ratio;
    result.SHCoeff_l1_m0 = multiplicand.SHCoeff_l1_m0 * ratio;
    result.SHCoeff_l1_m1 = multiplicand.SHCoeff_l1_m1 * ratio;
    result.SHCoeff_l2_m_2 = multiplicand.SHCoeff_l2_m_2 * ratio;
    result.SHCoeff_l2_m_1 = multiplicand.SHCoeff_l2_m_1 * ratio;
    result.SHCoeff_l2_m0 = multiplicand.SHCoeff_l2_m0 * ratio;
    result.SHCoeff_l2_m1 = multiplicand.SHCoeff_l2_m1 * ratio;
    result.SHCoeff_l2_m2 = multiplicand.SHCoeff_l2_m2 * ratio;
	
    return result;
}

float3 shMultiply(SHCoeff sh1, SHCoeff sh2)
{
    float3 result = sh1.SHCoeff_l0_m0 * sh2.SHCoeff_l0_m0 +
	sh1.SHCoeff_l1_m_1 * sh2.SHCoeff_l1_m_1 +
    sh1.SHCoeff_l1_m0 * sh2.SHCoeff_l1_m0 +
    sh1.SHCoeff_l1_m1 * sh2.SHCoeff_l1_m1 +
    sh1.SHCoeff_l2_m_2 * sh2.SHCoeff_l2_m_2 +
    sh1.SHCoeff_l2_m_1 * sh2.SHCoeff_l2_m_1 +
    sh1.SHCoeff_l2_m0 * sh2.SHCoeff_l2_m0+ 
    sh1.SHCoeff_l2_m1 * sh2.SHCoeff_l2_m1 +
    sh1.SHCoeff_l2_m2 * sh2.SHCoeff_l2_m2;
	
    return result;
}

SHCoeff shBlend(float ratio, SHCoeff lastFrameSHCoeff, SHCoeff thisFrameSHCoeff)
{
    SHCoeff resultCoeffs = (SHCoeff) 0.0f;
	
    resultCoeffs.SHCoeff_l0_m0 = ratio * lastFrameSHCoeff.SHCoeff_l0_m0 + (1.0f - ratio) * thisFrameSHCoeff.SHCoeff_l0_m0;
    resultCoeffs.SHCoeff_l1_m_1 = ratio * lastFrameSHCoeff.SHCoeff_l1_m_1 + (1.0f - ratio) * thisFrameSHCoeff.SHCoeff_l1_m_1;
    resultCoeffs.SHCoeff_l1_m0 = ratio * lastFrameSHCoeff.SHCoeff_l1_m0 + (1.0f - ratio) * thisFrameSHCoeff.SHCoeff_l1_m0;
    resultCoeffs.SHCoeff_l1_m1 = ratio * lastFrameSHCoeff.SHCoeff_l1_m1 + (1.0f - ratio) * thisFrameSHCoeff.SHCoeff_l1_m1;
    resultCoeffs.SHCoeff_l2_m_2 = ratio * lastFrameSHCoeff.SHCoeff_l2_m_2 + (1.0f - ratio) * thisFrameSHCoeff.SHCoeff_l2_m_2;
    resultCoeffs.SHCoeff_l2_m_1 = ratio * lastFrameSHCoeff.SHCoeff_l2_m_1 + (1.0f - ratio) * thisFrameSHCoeff.SHCoeff_l2_m_1;
    resultCoeffs.SHCoeff_l2_m0 = ratio * lastFrameSHCoeff.SHCoeff_l2_m0 + (1.0f - ratio) * thisFrameSHCoeff.SHCoeff_l2_m0;
    resultCoeffs.SHCoeff_l2_m1 = ratio * lastFrameSHCoeff.SHCoeff_l2_m1 + (1.0f - ratio) * thisFrameSHCoeff.SHCoeff_l2_m1;
    resultCoeffs.SHCoeff_l2_m2 = ratio * lastFrameSHCoeff.SHCoeff_l2_m2 + (1.0f - ratio) * thisFrameSHCoeff.SHCoeff_l2_m2;
	
    return resultCoeffs;
}