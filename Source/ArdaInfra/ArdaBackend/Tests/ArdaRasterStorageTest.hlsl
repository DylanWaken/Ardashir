RWStructuredBuffer<uint> Output : register(u0);

struct FArdaRasterStorageVertex
{
	float4 Position : SV_Position;
	float4 Color : COLOR0;
};

FArdaRasterStorageVertex RasterStorageVS(uint VertexId: SV_VertexID)
{
	const float2 Positions[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
	uint Ignored;
	InterlockedAdd(Output[0], 1, Ignored);
	Output[VertexId + 1] = 0xface0000u + VertexId;
	FArdaRasterStorageVertex Vertex;
	Vertex.Position = float4(Positions[VertexId], 0, 1);
	Vertex.Color = float4(1, 0, 0, 1);
	return Vertex;
}

float4 RasterStoragePS(FArdaRasterStorageVertex Input)
    : SV_Target0
{
	uint Ignored;
	InterlockedAdd(Output[0], 1, Ignored);
	uint Pixel = uint(Input.Position.y) * 4 + uint(Input.Position.x);
	Output[Pixel + 1] = 0xface0000u + Pixel;
	return Input.Color;
}
