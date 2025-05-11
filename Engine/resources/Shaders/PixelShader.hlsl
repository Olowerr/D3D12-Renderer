
#define UINT_MAX (~0u)
#define INVALID_UINT32 UINT_MAX

#define MAX_SHADOW_MAPS 32
#define MAX_POINT_SHADOW_CUBES 8

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
    uint shadowMapIdx;
    float farPlane;

    float3 position;
    float3 colour;
    float intensity;
    float2 attenuation;
};

struct DirectionalLight
{
    float4x4 viewProjMatrix;
    uint shadowMapIdx;

    float3 direction;
    float3 colour;
    float intensity;
};

struct SpotLight
{
    float4x4 viewProjMatrix;
    uint shadowMapIdx;
    
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
    float farPlane;
}


// Structured Buffers
StructuredBuffer<ObjectData> objectDatas : register(t1, space0);
StructuredBuffer<PointLight> pointLights : register(t3, space0);
StructuredBuffer<DirectionalLight> directionalLights : register(t4, space0);
StructuredBuffer<SpotLight> spotLights: register(t5, space0);


// Textures
Texture2D<unorm float4> textures[256] : register(t2, space1);
Texture2D<unorm float> shadowMaps[MAX_SHADOW_MAPS] : register(t6, space2);
TextureCube<unorm float> shadowMapCubes[MAX_POINT_SHADOW_CUBES] : register(t7, space3);


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

float getShadowValue(uint shadowMapIdx, float4x4 lightViewProjMatrix, float3 worldNormal, float3 worldToLight, float3 worldPosition)
{
    if (shadowMapIdx == INVALID_UINT32)
    {
        return 0.f;
    }

    float4 worldLightNDC = mul(float4(worldPosition, 1.f), lightViewProjMatrix);

    worldLightNDC.xyz /= worldLightNDC.w;
    worldLightNDC.xy = float2(worldLightNDC.x * 0.5f + 0.5f, worldLightNDC.y * -0.5f + 0.5f);
        
    float shadowMapDepth = shadowMaps[shadowMapIdx].SampleLevel(pointSampler, worldLightNDC.xy, 0.f).r;
            
    float bias = 0.000001f * tan(acos(max(dot(worldNormal, worldToLight), 0.f)));
    bias = clamp(bias, 0, 0.00001f);

    return shadowMapDepth > worldLightNDC.z - bias;
}

float getShadowValueCube(uint shadowMapIdx, float3 lightVec, float distToLight, float farPlane)
{
    if (shadowMapIdx == INVALID_UINT32)
    {
        return 0.f;
    }

    float shadowMapDepth = shadowMapCubes[shadowMapIdx].SampleLevel(pointSampler, lightVec, 0.f).r;
    shadowMapDepth *= farPlane;
    
    return shadowMapDepth > distToLight;
}

float4 main(InputData input) : SV_TARGET
{
    uint normalMapTextureIdx = objectDatas[input.instanceID].normalMapIdx;
    float3 worldNormal = sampleNormalMap(normalMapTextureIdx, input.uv, input.tbnMatrix);
    float3 vertexNormal = normalize(input.tbnMatrix[2].xyz);
    
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
        
        float shadowValue = getShadowValueCube(pointLight.shadowMapIdx, -worldToLight, distance, pointLight.farPlane);
        
        
        float dotty = max(dot(worldToLight, worldNormal), 0.f);
        float attentuation = 1.f / (1.f + pointLight.attenuation.x + pointLight.attenuation.y * distance * distance);
        
        diffuseLight += shadowValue * pointLight.colour * pointLight.intensity * dotty * attentuation;
        
        
        float3 lightReflection = reflect(-worldToLight, worldNormal);
        float specularIntensity = max(dot(lightReflection, worldToCamera), 0.f);
        specularIntensity = pow(specularIntensity, specularExpontent);
        
        specularLight += shadowValue * pointLight.colour * pointLight.intensity * specularIntensity * attentuation;
    }

    
    for (i = 0; i < numDirectionalLights; i++)
    {
        DirectionalLight dirLight = directionalLights[i];
        
        float shadowValue = getShadowValue(dirLight.shadowMapIdx, dirLight.viewProjMatrix, worldNormal, dirLight.direction, input.worldPosition);


        float dotty = max(dot(dirLight.direction, worldNormal), 0.f);
        diffuseLight += shadowValue * dirLight.colour * dirLight.intensity * dotty;
       

        float3 lightReflection = reflect(-dirLight.direction, worldNormal);
        float specularIntensity = max(dot(lightReflection, worldToCamera), 0.f);
        specularIntensity = pow(specularIntensity, specularExpontent);
        

        specularLight += shadowValue * dirLight.colour * dirLight.intensity * specularIntensity;
    }

    
    for (i = 0; i < numSpotLights; i++)
    {
        SpotLight spotLight = spotLights[i];
        
        float3 worldToLight = spotLight.position - input.worldPosition;
        
        float distance = length(worldToLight);
        worldToLight /= distance;
        
        if (dot(-worldToLight, spotLight.direction) < spotLight.cosineSpreadAngle)
        {
            continue; // Change to float value which we multiple by?
        }

        float shadowValue = getShadowValue(spotLight.shadowMapIdx, spotLight.viewProjMatrix, worldNormal, worldToLight, input.worldPosition);
        
        
        float dotty = max(dot(worldToLight, worldNormal), 0.f);
        float attenuation = 1.f / (1.f + spotLight.attenuation.x * distance + spotLight.attenuation.y * distance * distance);
        

        diffuseLight += shadowValue * spotLight.colour * spotLight.intensity * dotty * attenuation;
       
        
        float3 lightReflection = reflect(-worldToLight, worldNormal);
        float specularIntensity = max(dot(lightReflection, worldToCamera), 0.f);
        specularIntensity = pow(specularIntensity, specularExpontent);
        
        specularLight += shadowValue * spotLight.colour * spotLight.intensity * specularIntensity * attenuation;
    }

    
    return float4(materialDiffuse * (ambientLight + diffuseLight + specularLight), 1.f);
}
