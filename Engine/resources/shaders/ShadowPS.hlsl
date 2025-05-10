
struct InputData
{
    float4 svPosition : SV_POSITION;
    float3 worldPosition : WORLD_POS;
};

cbuffer RenderDataCBuffer : register(b0, space0)
{
    float4x4 viewProjMatrix;
    float3 cameraPos;
    uint numPointlights;
    float3 cameraDir;
    uint numDirectionallights;
    uint numSpotLights;
    float farPlane;
}

float main(InputData input) : SV_Depth
{
    return length(input.worldPosition - cameraPos) / farPlane;
}
