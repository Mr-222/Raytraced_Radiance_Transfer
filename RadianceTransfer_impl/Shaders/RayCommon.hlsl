// Hit information, aka ray payload
// This sample only carries a shading color and hit distance.
// Note that the payload should be kept as small as possible,
// and that its size must be declared in the corresponding
// D3D12_RAYTRACING_SHADER_CONFIG pipeline subobjet.
struct HitInfo
{
    float visibility;
    //int vid;
};

// Attributes output by the raytracing when hitting a surface,
// here the barycentric coordinates
struct Attributes
{
    float2 bary;
};

struct Vertex
{
    float3 PosL;
    float3 NormalL;
    float2 TexC;
    float3 TangentU;
};