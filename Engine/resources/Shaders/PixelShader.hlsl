
// Structs
struct InputData
{
    float4 svPosition : SV_POSITION;
    float3 worldPosition : WORLD_POS;
    float3 worldNormal : WORLD_NORMAL;
    float2 uv : UV;
    uint instanceID : SV_InstanceID;
};

struct ObjectData
{
    float4x4 objectMatrix;
    uint textureIdx;
};


// CBuffers
cbuffer RenderDataCBuffer : register(b0, space0)
{
    float4x4 viewProjMatrix;
    float3 cameraPos;
    float pad0;
    float3 cameraDir;
    float pad1;
}


// Structured Buffers
StructuredBuffer<ObjectData> objectDatas : register(t1, space0);


// Textures
Texture2D<unorm float4> textures[256] : register(t2, space0);


// Samplers
SamplerState sampy : register(s0, space0);


float4 main(InputData input) : SV_TARGET
{
    uint textureIdx = objectDatas[input.instanceID].textureIdx;

    return float4(textures[textureIdx].Sample(sampy, input.uv).rgb, 1.f);
}
