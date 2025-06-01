
// Structs
struct InputVertex
{
	float3 position;
	float3 normal;
	float2 uv;
	float3 tangent;
	float3 biTangent;
};

struct OutputVertex
{
	float4 svPosition : SV_POSITION;
	float3 worldPosition : WORLD_POS;
    float2 uv : UV;
    float3x3 tbnMatrix : TBN_MATRIX;
    uint objDataIndex : OBJ_DATA_INDEX;
};

struct ObjectData
{
	float4x4 objectMatrix;
    uint diffuseTextureIdx;
    uint normalMapIdx;
};


// CBuffers
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


// Structured Buffers
StructuredBuffer<InputVertex> verticies : register(t0, space0);
StructuredBuffer<ObjectData> objectDatas : register(t1, space0);
StructuredBuffer<uint> batchedObjDataIndicies : register(t8, space0);


// --- Functions

OutputVertex main(uint vertexId : SV_VERTEXID)
{
	OutputVertex output;
    output.objDataIndex = batchedObjDataIndicies[vertexId];
	
    InputVertex inputVertex = verticies[vertexId];
    float4x4 worldMatrix = objectDatas[output.objDataIndex].objectMatrix;
	
    output.worldPosition = mul(float4(inputVertex.position, 1.f), worldMatrix).xyz;
	output.svPosition = mul(float4(output.worldPosition, 1.f), viewProjMatrix);
	
    output.uv = inputVertex.uv;

    float3 worldNormal = normalize(mul(float4(inputVertex.normal, 0.f), worldMatrix)).xyz;
    float3 worldTangent = normalize(mul(float4(inputVertex.tangent, 0.f), worldMatrix)).xyz;
    float3 worldBiTangent = normalize(mul(float4(inputVertex.biTangent, 0.f), worldMatrix)).xyz;
    output.tbnMatrix = float3x3(worldTangent, worldBiTangent, worldNormal);
	
	return output;
}
