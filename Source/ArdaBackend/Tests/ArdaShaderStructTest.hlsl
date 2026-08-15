RWStructuredBuffer<uint> Output : register(u0);

[numthreads(1, 1, 1)]
void ShaderStructTestCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    Output[dispatchThreadId.x] = 0xA2DA;
}

float4 PipelineStateTestVS(uint vertexId : SV_VertexID) : SV_Position
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(0.0, 1.0),
        float2(1.0, -1.0)
    };
    return float4(positions[vertexId], 0.0, 1.0);
}

float4 PipelineStateTestPS() : SV_Target
{
    return float4(1.0, 0.0, 1.0, 1.0);
}
