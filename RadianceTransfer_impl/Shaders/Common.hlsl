//***************************************************************************************
// Common.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

// Include structures and functions for lighting.
#include "LightingUtil.hlsl"
#include "RandomNumber.hlsl"
#include "SHUtil.hlsl"

struct MaterialData
{
	float4   DiffuseAlbedo;
	float3   FresnelR0;
	float    Roughness;
	float4x4 MatTransform;
	uint     DiffuseMapIndex;
	uint     NormalMapIndex;
	uint     MatPad1;
	uint     MatPad2;
};

TextureCube gCubeMap : register(t0);
Texture2D gDepthMap : register(t1);
// Texture space visibility
Texture2D textureSpaceVisibility4 : register(t2);

// An array of textures, which is only supported in shader model 5.1+.  Unlike Texture2DArray, the textures
// in this array can be different sizes and formats, making it more flexible than texture arrays.
Texture2D gTextureMaps[10] : register(t3);

// Put in space1, so the texture array does not overlap with these resources.  
// The texture array will occupy registers t0, t1, ..., t3 in space0. 
StructuredBuffer<MaterialData> gMaterialData : register(t0, space1);

RWStructuredBuffer<SHCoeff> gSHCoeffsEnv : register(u0);
RWStructuredBuffer<SHCoeff> gTemporalSHCoeffsObject : register(u1);
RWStructuredBuffer<SHCoeff> gThisFrameSHCoeffsObject : register(u5);
RWStructuredBuffer<float4x4> gVisibility4x4 : register(u3);
RWStructuredBuffer<RandomResult> gRandomState : register(u4);

// Screen space intermediate shCoeffs
RWTexture2D<float4> screenSpaceIntermediateSHCoeffs[9] : register(u0, space1);
// Screen space this frame shCoeffs
RWTexture2D<float4> screenSpaceThisFrameSHCoeffs[9] : register(u0, space2);
// Screen space last frame shCoeffs
RWTexture2D<float4> screenSpaceLastFrameSHCoeffs[9] : register(u0, space3);
//Screen space this frame filtered shCoeffs
RWTexture2D<float4> screenSpaceFilteredHorzSHCoeffs[9] : register(u0, space4);
//Screen space this frame filtered shCoeffs
RWTexture2D<float4> screenSpaceFilteredVertSHCoeffs[9] : register(u0, space6);
// G-Buffer for spatial filtering, [0] stores this pixel's world position, [1] stores normal
RWTexture2D<float4> gBuffer[2] : register(u0, space5);

SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

// Constant data that varies per frame.
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gInvWorld;
	float4x4 gTexTransform;
    float4x4 gLastFrameWorld;
	uint gMaterialIndex;
	uint gVertexOffset;
	uint gObjId;
	uint gObjPad;
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
    float4x4 gInvWorld1;
    float4x4 gLastFrameWorld1;
    float4x4 gInvWorld2;
    float4x4 gLastFrameWorld2;
    float4x4 gInvWorld3;
    float4x4 gLastFrameWorld3;
};