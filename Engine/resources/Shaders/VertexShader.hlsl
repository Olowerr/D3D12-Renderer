
// Structs
struct InputVertex
{
	float3 position;
	float3 normal;
	float2 uv;
};

struct OutputVertex
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
	uint numPointlights;
	float3 cameraDir;
	uint numDirectionallights;
}


// Structured Buffers
StructuredBuffer<InputVertex> verticies : register(t0, space0);
StructuredBuffer<ObjectData> objectDatas : register(t1, space0);


OutputVertex main(uint vertexId : SV_VERTEXID, uint instanceID : SV_INSTANCEID)
{
	OutputVertex output;

    output.worldPosition = mul(float4(verticies[vertexId].position, 1.f), objectDatas[instanceID].objectMatrix);
	output.svPosition = mul(float4(output.worldPosition, 1.f), viewProjMatrix);
	
    output.worldNormal = mul(float4(verticies[vertexId].normal, 0.f), objectDatas[instanceID].objectMatrix);
    output.uv = verticies[vertexId].uv;
	
    output.instanceID = instanceID;

	return output;
}
