
// CBuffers
cbuffer mipData : register(b0, space0)
{
    float uavIdxAndSrvMip;
}

// SRVs
Texture2D srvMip : register(t0, space0);

// UAVs
// Index 0 here is mip 1 (2nd largest), since the descriptorRange starts at mip 1
RWTexture2D<unorm float4> uavMips[16] : register(u0, space0);

// Samplers
SamplerState sampy : register(s0, space0);


// --- Functions

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 mipSize = uint2(0, 0);
    uavMips[uavIdxAndSrvMip].GetDimensions(mipSize.x, mipSize.y);
    
    if (DTid.x >= mipSize.x || DTid.y >= mipSize.y)
    {
        return;
    }
    
    float2 uv = float2(DTid.xy + float2(0.5f, 0.5f)) / (float2) mipSize;

    float3 colour = srvMip.SampleLevel(sampy, uv, uavIdxAndSrvMip).rgb;
    uavMips[uavIdxAndSrvMip][DTid.xy] = float4(colour, 1.f);
}
