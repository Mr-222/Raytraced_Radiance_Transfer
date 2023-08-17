#include "RayCommon.hlsl"

[shader("closesthit")]
void ClosestHit(inout HitInfo payload, Attributes attrib)
{
    payload.visibility = 0.0f;
}