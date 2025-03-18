
struct InputVertex
{
	float3 position;
	float4 colour;
};

struct OutputVertex
{
	float4 svPosition : SV_POSITION;
	float3 worldPosition : WORLD_POS;
	float4 colour : COLOUR;
};
StructuredBuffer<InputVertex> verticies : register(t3, space0);

cbuffer RenderDataCBuffer : register(b0, space0)
{
	float4x4 viewProjMatrix;
	float3 cameraPos;
	float pad0;
	float3 cameraDir;
	float pad1;
}

struct ObjectData
{
	float4x4 objectMatrix;
};
StructuredBuffer<ObjectData> objectDatas : register(t1, space0);

OutputVertex main(uint vertexId : SV_VERTEXID, uint instanceId : SV_INSTANCEID)
{
	OutputVertex output;

	output.worldPosition = mul(float4(verticies[vertexId].position, 1.f), objectDatas[instanceId].objectMatrix);
	output.svPosition = mul(float4(output.worldPosition, 1.f), viewProjMatrix);

	output.colour = verticies[vertexId].colour;

	return output;
}
