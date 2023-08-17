#include "Sample.hlsl"
#include "RayCommon.hlsl"
#include "Util.hlsl"
#include "RandomNumber.hlsl"

// Visibility term
RWStructuredBuffer<float> gVisibility : register(u0);
RWStructuredBuffer<float4x4> gVisibility4x4 : register(u1);

// Random number state
RWStructuredBuffer<RandomResult> gRandomState : register(u2);

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t0);

StructuredBuffer<Vertex> Vertices : register(t1);

// Constant data that varies per frame.
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gInvWorld;
    float4x4 gTexTransform;
    float4x4 gLastFrameWorld;
    uint gMaterialIndex;
    uint gVertexOffset;
    uint gObjPad1;
    uint gObjPad2;
};

// Constant data that varies per material.
cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gLastFrameView;
    float4x4 gProj;
    float4x4 gLastFrameProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4x4 gLastFrameViewProj;
    float3 gEyePosW;
    uint indexOfSample; // which sample?
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;
};

[shader("raygeneration")]
void RayGen()
{
    uint vertexid = DispatchRaysIndex().x + gVertexOffset;
    uint rayIndex = DispatchRaysIndex().x;
    RandomResult result = gRandomState[vertexid];
    float4x4 visibility4x4;
    
    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            // Initialize the ray payload
            HitInfo payload;
            payload.visibility = 0.0f;
    
            float4 PositionW = mul(float4(Vertices[rayIndex].PosL, 1.0f), gWorld);
            // Assumes nonuniform scaling; otherwise, need to use inverse-transpose of world matrix.
            float3 NormalW = normalize(mul(Vertices[rayIndex].NormalL, (float3x3) gWorld));
    
            result = Random(result.state);
            float3 sampleVec = hemisphereSample_cos(result.u, result.v);
        
            sampleVec = normalize(FromNormalToWorld(sampleVec, NormalW));
    
            // Define a ray, consisting of origin, direction, and the min-max distance values
            RayDesc ray;
            ray.Origin = PositionW.xyz;
            ray.Direction = sampleVec;
            ray.TMin = 0.00001; // Offset a little in case of self-intersection.
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
        
            visibility4x4[i][j] = payload.visibility;
        }
    }
         
    gVisibility4x4[vertexid] = visibility4x4;
}
