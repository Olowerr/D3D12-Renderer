
cbuffer ColourCBuffer : register(b1, space0)
{
	float4 cbColour;
}

float4 main(float4 position : SV_POSITION, float4 colour : COLOUR) : SV_TARGET
{
	return colour + cbColour;
}