
cbuffer ColourCBuffer : register(b2, space0)
{
	float4 cbColour;
}

struct ObjectData
{
	float4x4 objectMatrix;
};
StructuredBuffer<ObjectData> objectDatas : register(t1, space0);

struct InputData
{
	float4 svPosition : SV_POSITION;
	float3 worldPosition : WORLD_POS;
	float4 colour : COLOUR;
};

float4 main(InputData input) : SV_TARGET
{
	return input.colour + cbColour;
}
