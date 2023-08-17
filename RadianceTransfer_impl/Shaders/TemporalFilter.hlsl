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
    float2 TexC : TEXCOORD;
};

float3 temporalFilter(float3 thisFrameColor, float2 uv, float ratio)
{
    uint width, height;
    screenSpaceFilteredHorzSHCoeffs[0].GetDimensions(width, height);
    
    float objectId = gBuffer[1][uv].w;
    float4x4 invWorld = (float4x4) 0.0f;
    float4x4 lastFrameWorld = (float4x4) 0.0f;
    
    if (objectId == 1.0f)
    {
        invWorld = gInvWorld1;
        lastFrameWorld = gLastFrameWorld1;
    }
    else if (objectId == 2.0f)
    {
        invWorld = gInvWorld2;
        lastFrameWorld = gLastFrameWorld2;
    }
    else if (objectId == 3.0f)
    {
        invWorld = gInvWorld3;
        lastFrameWorld = gLastFrameWorld3;
    }

    float4 position = float4(gBuffer[0].Load(int3(uv, 0)).xyz, 1.0f);
    position = mul(position, invWorld);
    position = mul(position, lastFrameWorld);
    position = mul(position, gLastFrameViewProj);
    position.xyz /= position.w;
    position.x = (position.x + 1.0f) / 2.0f;
    // Viewport's origin is defined at top left corner, so inverse y.
    position.y = -position.y;
    position.y = (position.y + 1.0f) / 2.0f;
    position.x = position.x * width;
    position.y = position.y * height;
    
    float4 lastFrameColor = screenSpaceLastFrameSHCoeffs[0].Load(int3(position.xy, 0));
    // Detect temporal failure.
    if ((lastFrameColor.x == 0 && lastFrameColor.y == 0 && lastFrameColor.z == 0) || lastFrameColor.w != objectId)
        ratio = 0;
    // Temporal clamping.
    else
    {
        float3 mean = float3(0.0f, 0.0f, 0.0f);
        float variance = 0.0f;
        int r = 3;
        for (int i = -r; i <= r; ++i)
        {
            for (int j = -r; j <= r; ++j)
            {
                mean += screenSpaceThisFrameSHCoeffs[1][uint2(uv.x + i, uv.y + j)].xyz;
            }
        }
        mean /= 49.0f;
        
        for (int i = -r; i <= r; ++i)
        {
            for (int j = -r; j <= r; ++j)
            {
                variance += pow(distance(mean, screenSpaceThisFrameSHCoeffs[1][uint2(uv.x + i, uv.y + j)].xyz), 2);
            }
        }
        variance /= 48.0f;
        variance = sqrt(variance);
        lastFrameColor.xyz = clamp(lastFrameColor.xyz, mean - (variance * sigmaClamp), mean + (variance * sigmaClamp));
    }
    float3 color = ratio * lastFrameColor.xyz + (1.0f - ratio) * thisFrameColor;
    screenSpaceIntermediateSHCoeffs[0][uv] = float4(color, objectId);
    return color;
}

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;
    vout.PosH = float4(vin.PosL, 1.0f);
    vout.TexC = vin.TexC;
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    float2 uv = pin.TexC;
    uint width, height;
    screenSpaceFilteredHorzSHCoeffs[0].GetDimensions(width, height);
    uv.x *= width;
    uv.y *= height;
    
    if (gBuffer[1][uv].w == 0)
        discard;
    
    float3 color = screenSpaceThisFrameSHCoeffs[1][uv];
    color = temporalFilter(color, uv, 0.9);
    return float4(color, 1.0f);
}