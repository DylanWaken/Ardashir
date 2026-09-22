float4 RecipeVertexPosition(uint Vertex)
{
	const float2 P[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
	return float4(P[Vertex], 0, 1);
}

float4 RecipeVS(uint Vertex: SV_VertexID)
    : SV_Position
{
	return RecipeVertexPosition(Vertex);
}

RWStructuredBuffer<float4> CapturedPositions : register(u0);

struct FArdaCaptureParameters
{
	uint VertexCount;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<FArdaCaptureParameters> Capture : register(b0);

[numthreads(3, 1, 1)]
void RecipeCaptureVerticesCS(uint3 Thread: SV_DispatchThreadID)
{
	const uint Vertex = Thread.x;
	if (Vertex < Capture.VertexCount)
	{
		CapturedPositions[Vertex] = RecipeVertexPosition(Vertex);
	}
}

float4 RecipeRasterPS()
    : SV_Target
{
	return float4(0, 1, 0, 1);
}
