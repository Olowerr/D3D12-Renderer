
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

struct DirectionalLight
{
    float3 direction;
    float3 colour;
    float intensity;
};


// CBuffers
cbuffer RenderDataCBuffer : register(b0, space0)
{
    float4x4 viewProjMatrix;
    float3 cameraPos;
    uint numPointlights;
    float3 cameraDir;
    uint numDirectionallights;
}


// Structured Buffers
StructuredBuffer<ObjectData> objectDatas : register(t1, space0);
StructuredBuffer<PointLight> pointLights : register(t3, space0);
StructuredBuffer<DirectionalLight> directionalLights : register(t4, space0);


// Textures
Texture2D<unorm float4> textures[256] : register(t2, space1);


// Samplers
SamplerState sampy : register(s0, space0);


float4 main(InputData input) : SV_TARGET
{
    uint diffuseTextureIdx = objectDatas[input.instanceID].textureIdx;
    
    float3 diffuse = textures[diffuseTextureIdx].Sample(sampy, input.uv).rgb;
    float3 ambient = diffuse * 0.2f;
    
    float3 light = float3(0.f, 0.f, 0.f);
    
    uint i = 0;
    for (i = 0; i < numPointlights; i++)
    {
        PointLight pointLight = pointLights[i];
        
        float3 worldToLight = pointLight.position - input.worldPosition;
        float distance = length(worldToLight);
        
        float dotty = max(dot(worldToLight / distance, input.worldNormal), 0.f);
        float attentuation = 1.f / (1.f + pointLight.attenuation.x + pointLight.attenuation.y * distance * distance);
        
        light += pointLight.colour * pointLight.intensity * dotty * attentuation;
    }
    
    for (i = 0; i < numDirectionallights; i++)
    {
        DirectionalLight dirLight = directionalLights[i];
        
        float dotty = max(dot(dirLight.direction, input.worldNormal), 0.f);
        light += dirLight.colour * dirLight.intensity * dotty;
    }
    
    return float4(light * diffuse + ambient, 1.f);
}
