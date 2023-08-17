#include "Sample.hlsl"
#include "RayCommon.hlsl"
#include "Util.hlsl"
#include "RandomNumber.hlsl"

// Visibility term
RWTexture2D<float4> gVisibility4: register(u0);

// Random number state
RWStructuredBuffer<RandomResult> gRandomState : register(u1);

// G-Buffer, [0] stores this pixel's world position, [1] stores normal
RWTexture2D<float4> gBuffer[2] : register(u2);

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t0);

[shader("raygeneration")]
void RayGen()
{   
    // Get the location within the dispatched 2D grid of work items
    // (often maps to pixels, so this could represent a pixel coordinate).
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint stateIndex = launchIndex.x + 1920 * launchIndex.y;
    float2 dims = float2(DispatchRaysDimensions().xy);
    
    float4 PositionW = gBuffer[0][launchIndex];
    float3 NormalW = gBuffer[1][launchIndex].xyz;
    
    if (PositionW.w != 1.0f)
    {
        return;
    }
    
    RandomResult result = gRandomState[stateIndex];
    float4 visibility4 = float4(1.0f, 1.0f, 1.0f, 1.0f);
    
    for (int i = 0; i < 4; ++i)
    {
        // Initialize the ray payload
        HitInfo payload;
        payload.visibility = 0.0f;
        
        result = Random(result.state);
        float3 sampleVec = hemisphereSample_cos(result.u, result.v);
        
        sampleVec = normalize(FromNormalToWorld(sampleVec, NormalW));
    
        // Define a ray, consisting of origin, direction, and the min-max distance values
        RayDesc ray;
        ray.Origin = PositionW.xyz;
        ray.Direction = sampleVec;
        ray.TMin = 0.0001; // Offset a little in case of self-intersection.
        ray.TMax = 1000000;

        // Trace the ray
        TraceRay(
        // Parameter name: AccelerationStructure
        // Acceleration structure
        SceneBVH,

        // Parameter name: RayFlags
        // Flags can be used to specify the behavior upon hitting a surface
        RAY_FLAG_NONE,

        // Parameter name: InstanceInclusionMask
        // Instance inclusion mask, which can be used to mask out some geometry to
        // this ray by and-ing the mask with a geometry mask. The 0xFF flag then
        // indicates no geometry will be masked
        0xFF,

        // Parameter name: RayContributionToHitGroupIndex
        // Depending on the type of ray, a given object can have several hit
        // groups attached (ie. what to do when hitting to compute regular
        // shading, and what to do when hitting to compute shadows). Those hit
        // groups are specified sequentially in the SBT, so the value below
        // indicates which offset (on 4 bits) to apply to the hit groups for this
        // ray. In this sample we only have one hit group per object, hence an
        // offset of 0.
        0,

        // Parameter name: MultiplierForGeometryContributionToHitGroupIndex
        // The offsets in the SBT can be computed from the object ID, its instance
        // ID, but also simply by the order the objects have been pushed in the
        // acceleration structure. This allows the application to group shaders in
        // the SBT in the same order as they are added in the AS, in which case
        // the value below represents the stride (4 bits representing the number
        // of hit groups) between two consecutive objects.
        0,

        // Parameter name: MissShaderIndex
        // Index of the miss shader to use in case several consecutive miss
        // shaders are present in the SBT. This allows to change the behavior of
        // the program when no geometry have been hit, for example one to return a
        // sky color for regular rendering, and another returning a full
        // visibility value for shadow rays. This sample has only one miss shader,
        // hence an index 0
        0,

        // Parameter name: Ray
        // Ray information to trace
        ray,

        // Parameter name: Payload
        // Payload associated to the ray, which will be used to communicate
        // between the hit/miss shaders and the raygen
        payload);
        
        visibility4[i] = payload.visibility;
    }
    
    gVisibility4[launchIndex] = visibility4;
}
