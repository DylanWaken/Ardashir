RWStructuredBuffer<uint> Output : register(u0);

[numthreads(1, 1, 1)]
void ShaderStructTestCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    Output[dispatchThreadId.x] = 0xA2DA;
}

Texture2D<float4> DirectHeapTexture : register(t0, space0);
RWStructuredBuffer<uint> DirectHeapOutput : register(u0, space0);
SamplerState DirectHeapSampler : register(s0, space1);

[numthreads(1, 1, 1)]
void DirectHeapSamplerTestCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const float4 value = DirectHeapTexture.SampleLevel(
        DirectHeapSampler, float2(0.5, 0.5), 0.0);
    DirectHeapOutput[dispatchThreadId.x] = value.r > 0.2 ? 0x5A4D : 0;
}

struct WorkGraphInputRecord
{
    uint Value;
};

RWStructuredBuffer<uint> WorkGraphOutput : register(u0);

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeDispatchGrid(1, 1, 1)]
[NumThreads(1, 1, 1)]
void WorkGraphMain(
    DispatchNodeInputRecord<WorkGraphInputRecord> inputRecord)
{
    WorkGraphOutput[0] = inputRecord.Get().Value;
}

RWTexture2D<float4> VulkanLayoutOutput : register(u0);

[numthreads(1, 1, 1)]
void VulkanLayoutTestCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    VulkanLayoutOutput[dispatchThreadId.xy] =
        float4(0.125, 0.25, 0.5, 1.0);
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

struct MeshPipelineVertex
{
    float4 Position : SV_Position;
};

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void MeshPipelineTestMS(
    uint3 groupThreadId : SV_GroupThreadID,
    out vertices MeshPipelineVertex vertices[3],
    out indices uint3 triangles[1])
{
    SetMeshOutputCounts(3, 1);
    if (groupThreadId.x == 0)
    {
        vertices[0].Position = float4(-1.0, -1.0, 0.0, 1.0);
        vertices[1].Position = float4(-1.0, 3.0, 0.0, 1.0);
        vertices[2].Position = float4(3.0, -1.0, 0.0, 1.0);
        triangles[0] = uint3(0, 1, 2);
    }
}

float4 MeshPipelineTestPS() : SV_Target
{
    return float4(0.0, 1.0, 0.0, 1.0);
}

cbuffer BindingSpaceConstants : register(b0)
{
    float4 BindingSpaceOffset;
};

float4 BindingSpaceVS(uint vertexId : SV_VertexID) : SV_Position
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(0.0, 1.0),
        float2(1.0, -1.0)
    };
    return float4(positions[vertexId] + BindingSpaceOffset.xy, 0.0, 1.0);
}

Texture2D<float4> BindingSpaceTexture : register(t0);

float4 BindingSpacePS() : SV_Target
{
    return BindingSpaceTexture.Load(int3(0, 0, 0));
}
