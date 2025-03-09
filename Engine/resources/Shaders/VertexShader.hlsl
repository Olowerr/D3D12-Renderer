
static float3 VERTICIES[] = {
	float3(-0.5f, -0.5f, 0.f),
	float3(0.f, 0.5f, 0.f),
	float3(0.5f, -0.5f, 0.0f),
};

float4 main(uint vertexId : SV_VERTEXID) : SV_POSITION
{
	return float4(VERTICIES[vertexId], 1.f);
}