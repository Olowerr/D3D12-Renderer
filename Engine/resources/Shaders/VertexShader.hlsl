
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

StructuredBuffer<InputVertex> verticies : register(t0, space0);

OutputVertex main(uint vertexId : SV_VERTEXID)
{
	OutputVertex output;
	output.svPosition = float4(verticies[vertexId].position, 1.f);
	output.colour = verticies[vertexId].colour;

	return output;
}