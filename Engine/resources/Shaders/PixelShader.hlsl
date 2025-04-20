
// Structs
struct InputData
{
    float4 svPosition : SV_POSITION;
    float3 worldPosition : WORLD_POS;
    float3 worldNormal : WORLD_NORMAL;
    float2 uv : UV;
    uint instanceID : SV_InstanceID;
};

struct ObjectData
{
    float4x4 objectMatrix;
    uint textureIdx;
};

struct PointLight
{
    float3 position;
    float3 colour;
    float intensity;
    float2 attenuation;
};


// CBuffers
cbuffer RenderDataCBuffer : register(b0, space0)
{
    float4x4 viewProjMatrix;
    float3 cameraPos;
    uint numPointlights;
    float3 cameraDir;
    float pad1;
}


// Structured Buffers
StructuredBuffer<ObjectData> objectDatas : register(t1, space0);
StructuredBuffer<PointLight> pointLights : register(t2, space0);


// Textures
Texture2D<unorm float4> textures[256] : register(t3, space0);


// Samplers
SamplerState sampy : register(s0, space0);


float4 main(InputData input) : SV_TARGET
{
    uint diffuseTextureIdx = objectDatas[input.instanceID].textureIdx;
    
    float3 diffuse = textures[diffuseTextureIdx].Sample(sampy, input.uv).rgb;
    float3 ambient = diffuse * 0.2f;
    
    uint i = 0;
    for (i = 0; i < numPointlights; i++)
    {
        PointLight light = pointLights[i];
        
        float3 worldToLight = light.position - input.worldPosition;
        float distance = length(worldToLight);
        
        float dotty = dot(worldToLight / distance, input.worldNormal);
        float attentuation = 1.f / (1.f + light.attenuation.x + light.attenuation.y * distance * distance);
        
        diffuse *= light.colour * light.intensity * dotty * attentuation;
    }

    return float4(ambient + diffuse, 1.f);
}
