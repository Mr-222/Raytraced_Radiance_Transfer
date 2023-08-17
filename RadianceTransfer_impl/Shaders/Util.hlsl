//---------------------------------------------------------------------------------------
// Transforms a normal map sample to world space.
//---------------------------------------------------------------------------------------
float3 NormalSampleToWorldSpace(float3 normalMapSample, float3 unitNormalW, float3 tangentW)
{
	// Uncompress each component from [0,1] to [-1,1].
    float3 normalT = 2.0f * normalMapSample - 1.0f;

	// Build orthonormal basis.
    float3 N = unitNormalW;
    float3 T = normalize(tangentW - dot(tangentW, N) * N);
    float3 B = cross(N, T);

    float3x3 TBN = float3x3(T, B, N);

	// Transform from tangent space to world space.
    float3 bumpedNormalW = mul(normalT, TBN);

    return bumpedNormalW;
}

//--------------------------------------------------------------------------------------
// Transform local to world space
//--------------------------------------------------------------------------------------
float3x3 computeLocalToWorld(float3 normal)
{
	// Using right-hand coord
    const float3 up = abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0.xx);
    const float3 xAxis = normalize(cross(up, normal));
    const float3 yAxis = cross(normal, xAxis);
    const float3 zAxis = normal;

    return float3x3(xAxis, yAxis, zAxis);
}

float3 FromNormalToWorld(float3 sampleVector, float3 unitNormalW)
{
    // Build orthonormal basis.
    float3x3 TBN = computeLocalToWorld(unitNormalW);
    
    return mul(sampleVector, TBN);
}

float3 FromNormalToWorld(float3 sampleVector, float3 unitNormalW, float3 tangentW)
{
    // Build orthonormal basis.
    float3 N = unitNormalW;
    float3 T = normalize(tangentW - dot(tangentW, N) * N);
    float3 B = cross(N, T);

    float3x3 TBN = float3x3(T, B, N);
    
    return mul(sampleVector, TBN);
}

