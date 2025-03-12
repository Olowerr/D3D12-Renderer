
struct InputVertex
{
	float3 position;
	float4 colour;
};

struct OutputVertex
{
	float4 svPosition : SV_POSITION;
	float4 colour : COLOUR;
};

StructuredBuffer<InputVertex> verticies : register(t1, space0);
cbuffer RenderData : register(b0, space0)
{
	float4x4 viewProjMatrix;
	float3 cameraPos;
	float pad0;
	float3 cameraDir;
	float pad1;
}

OutputVertex main(uint vertexId : SV_VERTEXID)
{
	OutputVertex output;
	output.svPosition = mul(float4(verticies[vertexId].position, 1.f), viewProjMatrix);
	output.colour = verticies[vertexId].colour;

	return output;
}