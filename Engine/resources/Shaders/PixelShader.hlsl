
cbuffer ColourCBuffer : register(b0, space0)
{
	float4 colour;
}

float4 main() : SV_TARGET
{
	return colour;
}