#include "RayCommon.hlsl"

[shader("miss")]
void Miss(inout HitInfo payload : SV_RayPayload)
{
    payload.visibility = 1.0f;
}