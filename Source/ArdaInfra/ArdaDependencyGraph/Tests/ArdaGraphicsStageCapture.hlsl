// Both entries call this transform, so fetching vertices captures the same values as drawing.
struct FArdaVertex
{
	float4 Position;
	float4 Color;
};

struct FArdaVertexOutput
{
	float4 Position : SV_Position;
	float4 Color : COLOR0;
};

StructuredBuffer<FArdaVertex> InputVertices : register(t0);
RWStructuredBuffer<FArdaVertex> CapturedVertices : register(u0);
RWStructuredBuffer<uint> CapturedFragments : register(u1);

struct FArdaCaptureParameters
{
	uint Capacity;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<FArdaCaptureParameters> CaptureParameters : register(b0);

FArdaVertex TransformVertex(FArdaVertex Input)
{
	FArdaVertex Result;
	Result.Position = float4(Input.Position.xy * 0.75, Input.Position.z, 1.0);
	Result.Color = Input.Color;
	return Result;
}

FArdaVertexOutput StageVS(uint Vertex: SV_VertexID)
{
	FArdaVertex Value = TransformVertex(InputVertices[Vertex]);
	FArdaVertexOutput Result;
	Result.Position = Value.Position;
	Result.Color = Value.Color;
	return Result;
}

[numthreads(64, 1, 1)]
void CaptureVerticesCS(uint3 Thread: SV_DispatchThreadID)
{
	if (Thread.x < CaptureParameters.Capacity)
	{
		CapturedVertices[Thread.x] = TransformVertex(InputVertices[Thread.x]);
	}
}

float4 StagePS(FArdaVertexOutput Input)
    : SV_Target0
{
	// Shared physical input at the same register exercises VS/PS binding assembly.
	return Input.Color * InputVertices[0].Color.w;
}

float4 CaptureFragmentsPS(FArdaVertexOutput Input)
    : SV_Target0
{
	// Header: attempted records, overflowed records, and two reserved words.
	// Every fragment reserves a unique slot, including overlapping primitives.
	uint Record;
	InterlockedAdd(CapturedFragments[0], 1, Record);
	if (Record < CaptureParameters.Capacity)
	{
		uint Address = 4 + Record * 8;
		[unroll]
		for (uint Component = 0; Component < 4; ++Component)
		{
			CapturedFragments[Address + Component] = asuint(Input.Position[Component]);
			CapturedFragments[Address + 4 + Component] = asuint(Input.Color[Component]);
		}
	}
	else
	{
		uint Ignored;
		InterlockedAdd(CapturedFragments[1], 1, Ignored);
	}
	return Input.Color;
}
