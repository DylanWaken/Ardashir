StructuredBuffer<float4> Positions : register(t0);

float4 UsageVS(uint Index: SV_VertexID)
    : SV_Position
{
	return Positions[Index];
}

float4 UsagePS()
    : SV_Target0
{
	return float4(0, 1, 0, 1);
}
