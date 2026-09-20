float4 RasterVS(uint Vertex: SV_VertexID)
    : SV_Position
{
	const float2 Positions[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
	return float4(Positions[Vertex], 0, 1);
}

float4 RasterPS()
    : SV_Target
{
	return float4(1, 0, 1, 1);
}

RWStructuredBuffer<uint> First : register(u2, space3);
RWStructuredBuffer<uint> Second : register(u5, space0);

[numthreads(1, 1, 1)]
void RegisteredCS(uint3 Thread: SV_DispatchThreadID)
{
	First[Thread.x] = 0xA2DA0000u + Thread.x;
	Second[Thread.x] = First[Thread.x] ^ 0x12345678u;
}

[numthreads(1, 1, 1)]
void MultiWriterReadWriteCS(uint3 Thread: SV_DispatchThreadID)
{
	First[Thread.x] += 17u + Thread.x;
}

RWStructuredBuffer<uint> Partitioned[2] : register(u0, space0);

struct FArdaSelection
{
	uint SelectedIndex;
	uint SelectedValue;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<FArdaSelection> Selection : register(b0, space1);

[numthreads(1, 1, 1)]
void BindlessCS(uint3 Thread: SV_DispatchThreadID)
{
	Partitioned[Selection.SelectedIndex][Thread.x] = Selection.SelectedValue + Thread.x;
}
