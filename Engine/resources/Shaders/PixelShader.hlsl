
// Structs
struct InputData
{
    float4 svPosition : SV_POSITION;
    float3 worldPosition : WORLD_POS;
    float2 uv : UV;
    float3x3 tbnMatrix : TBN_MATRIX;
    uint instanceID : SV_InstanceID;
};

struct ObjectData
{
    float4x4 objectMatrix;
    uint diffuseTextureIdx;
    uint normalMapIdx;
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

struct SpotLight
{
    float3 position;
    float3 direction;

    float3 colour;
    float intensity;
    float2 attenuation;
    float cosineSpreadAngle;
};


// CBuffers
cbuffer RenderDataCBuffer : register(b0, space0)
{
    float4x4 viewProjMatrix;
    float3 cameraPos;
    uint numPointLights;
    float3 cameraDir;
    uint numDirectionalLights;
    uint numSpotLights;
}


// Structured Buffers
StructuredBuffer<ObjectData> objectDatas : register(t1, space0);
StructuredBuffer<PointLight> pointLights : register(t3, space0);
StructuredBuffer<DirectionalLight> directionalLights : register(t4, space0);
StructuredBuffer<SpotLight> spotLights: register(t5, space0);


// Textures
Texture2D<unorm float4> textures[256] : register(t2, space1);


// Samplers
SamplerState pointSampler : register(s0, space0);
SamplerState anisotropicSampler : register(s1, space0);


// --- Functions

float3 sampleNormalMap(uint normalMapIdx, float2 uv, float3x3 tbnMatrix)
{
    // should use point sampler..?
    float3 normal = textures[normalMapIdx].Sample(pointSampler, uv).rgb;
    normal = normal * 2.f - float3(1.f, 1.f, 1.f);

    normal.y *= -1.f; // Flipping is correct for sponza, but isn't for many other normal maps

    return normalize(mul(normal, tbnMatrix));
}

float4 main(InputData input) : SV_TARGET
{
    uint normalMapTextureIdx = objectDatas[input.instanceID].normalMapIdx;
    float3 worldNormal = sampleNormalMap(normalMapTextureIdx, input.uv, input.tbnMatrix);
    
    uint diffuseTextureIdx = objectDatas[input.instanceID].diffuseTextureIdx;
    float3 materialDiffuse = textures[diffuseTextureIdx].Sample(anisotropicSampler, input.uv).rgb;

    
    float3 ambientLight = float3(0.2f, 0.2, 0.2f);
    float3 diffuseLight = float3(0.f, 0.f, 0.f);
    float3 specularLight = float3(0.f, 0.f, 0.f);
    float specularExpontent = 50.f; // temp
    
    float3 worldToCamera = normalize(cameraPos - input.worldPosition);
    
    uint i = 0;
    for (i = 0; i < numPointLights; i++)
    {
        PointLight pointLight = pointLights[i];
        
        float3 worldToLight = pointLight.position - input.worldPosition;

        float distance = length(worldToLight);
        worldToLight /= distance;
        
        float dotty = max(dot(worldToLight, worldNormal), 0.f);
        float attentuation = 1.f / (1.f + pointLight.attenuation.x + pointLight.attenuation.y * distance * distance);
        
        diffuseLight += pointLight.colour * pointLight.intensity * dotty * attentuation;
        
        
        float3 lightReflection = reflect(-worldToLight, worldNormal);
        float specularIntensity = max(dot(lightReflection, worldToCamera), 0.f);
        specularIntensity = pow(specularIntensity, specularExpontent);
        
        specularLight += pointLight.colour * pointLight.intensity * specularIntensity * attentuation;
    }

    
    for (i = 0; i < numDirectionalLights; i++)
    {
        DirectionalLight dirLight = directionalLights[i];
        
        float dotty = max(dot(dirLight.direction, worldNormal), 0.f);
        diffuseLight += dirLight.colour * dirLight.intensity * dotty;
       

        float3 lightReflection = reflect(-dirLight.direction, worldNormal);
        float specularIntensity = max(dot(lightReflection, worldToCamera), 0.f);
        specularIntensity = pow(specularIntensity, specularExpontent);
        
        specularLight += dirLight.colour * dirLight.intensity * specularIntensity;
    }

    
    for (i = 0; i < numSpotLights; i++)
    {
        SpotLight spotLight = spotLights[i];
        
        float3 worldToLight = spotLight.position - input.worldPosition;
        
        float distance = length(worldToLight);
        worldToLight /= distance;
        
        if (dot(-worldToLight, spotLight.direction) < spotLight.cosineSpreadAngle)
        {
            continue;
        }

        
        float dotty = max(dot(worldToLight, worldNormal), 0.f);
        float attenuation = 1.f / (1.f + spotLight.attenuation.x * distance + spotLight.attenuation.y * distance * distance);
        
        diffuseLight += spotLight.colour * spotLight.intensity * dotty * attenuation;
       
        
        float3 lightReflection = reflect(-worldToLight, worldNormal);
        float specularIntensity = max(dot(lightReflection, worldToCamera), 0.f);
        specularIntensity = pow(specularIntensity, specularExpontent);
        
        specularLight += spotLight.colour * spotLight.intensity * specularIntensity * attenuation;
    }

    
    return float4(materialDiffuse * (ambientLight + diffuseLight + specularLight), 1.f);
}
