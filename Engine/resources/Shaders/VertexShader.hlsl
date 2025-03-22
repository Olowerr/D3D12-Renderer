
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
};

struct ObjectData
{
	float4x4 objectMatrix;
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


// SRVs

StructuredBuffer<InputVertex> verticies : register(t0, space0);
StructuredBuffer<ObjectData> objectDatas : register(t1, space0);


OutputVertex main(uint vertexId : SV_VERTEXID, uint instanceId : SV_INSTANCEID)
{
	OutputVertex output;

	output.worldPosition = mul(float4(verticies[vertexId].position, 1.f), objectDatas[instanceId].objectMatrix);
	output.svPosition = mul(float4(output.worldPosition, 1.f), viewProjMatrix);
	
    output.worldNormal = mul(float4(verticies[vertexId].normal, 0.f), objectDatas[instanceId].objectMatrix);
    output.uv = verticies[vertexId].uv;

	return output;
}
