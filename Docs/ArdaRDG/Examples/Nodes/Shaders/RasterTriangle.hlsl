float4 RecipeVS(uint FArdaVertex: SV_VertexID)
    : SV_Position
{
	const float2 P[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
	return float4(P[FArdaVertex], 0, 1);
}

float4 RecipeRasterPS()
    : SV_Target
{
	return float4(0, 1, 0, 1);
}
