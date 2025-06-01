
// Structs
struct InputVertex
{
    float3 position;
    float3 normal;
    float2 uv;
    float3 tangent;
    float3 biTangent;
};

struct OutputVertex
{
    float4 svPosition : SV_POSITION;
    float3 worldPosition : WORLD_POS;
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
}


// Structured Buffers
StructuredBuffer<InputVertex> verticies : register(t0, space0);
StructuredBuffer<ObjectData> objectDatas : register(t1, space0);
StructuredBuffer<uint> batchedObjDataIndicies : register(t2, space0);


// --- Functions

OutputVertex main(uint vertexId : SV_VERTEXID)
{
    OutputVertex output;
    
    InputVertex inputVertex = verticies[vertexId];
    float4x4 worldMatrix = objectDatas[batchedObjDataIndicies[vertexId]].objectMatrix;

    output.worldPosition = mul(float4(inputVertex.position, 1.f), worldMatrix).xyz;
    output.svPosition = mul(float4(output.worldPosition, 1.f), viewProjMatrix);

    return output;
}
