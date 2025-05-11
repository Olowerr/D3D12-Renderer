
struct OutputVertex
{
    float4 svPosition : SV_POSITION;
    float3 worldPosition : WORLD_POS;
    uint renderTargetIdx : SV_RenderTargetArrayIndex;
};

cbuffer matrices : register(b0, space0)
{
    float4x4 viewProjMatrices[6];
    float3 lightPos;
    float farPlane;
}

[maxvertexcount(18)]
void main(triangle float3 input[3] : WORLD_POS, inout TriangleStream<OutputVertex> output)
{
	for (uint i = 0; i < 6; i++)
	{
        for (uint k = 0; k < 3; k++)
        {
            OutputVertex element;

            element.worldPosition = input[k];
            element.svPosition = mul(float4(element.worldPosition, 1.f), viewProjMatrices[i]);
            element.renderTargetIdx = i;

		    output.Append(element);
        }
        output.RestartStrip();
    }
}
