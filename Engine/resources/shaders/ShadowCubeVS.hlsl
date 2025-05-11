
// Structs
struct InputVertex
{
    float3 position;
    float3 normal;
    float2 uv;
    float3 tangent;
    float3 biTangent;
};

struct ObjectData
{
    float4x4 objectMatrix;
    uint diffuseTextureIdx;
    uint normalMapIdx;
};


// CBuffers
cbuffer RenderDataCBuffer : register(b0, space0)
{
    float4x4 viewProjMatrix;
    float3 cameraPos;
    uint numPointlights;
    float3 cameraDir;
    uint numDirectionallights;
    uint numSpotLights;
}


// Structured Buffers
StructuredBuffer<InputVertex> verticies : register(t0, space0);
StructuredBuffer<ObjectData> objectDatas : register(t1, space0);


// --- Functions

float3 main(uint vertexId : SV_VERTEXID, uint instanceID : SV_INSTANCEID) : WORLD_POS
{
    InputVertex inputVertex = verticies[vertexId];
    float4x4 worldMatrix = objectDatas[instanceID].objectMatrix;
	
    float3 worldPosition = mul(float4(inputVertex.position, 1.f), worldMatrix).xyz;
    return worldPosition;
}
