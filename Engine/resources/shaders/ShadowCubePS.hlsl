
struct InputData
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

float main(InputData input) : SV_Depth
{
    return length(input.worldPosition - lightPos) / farPlane;
}
