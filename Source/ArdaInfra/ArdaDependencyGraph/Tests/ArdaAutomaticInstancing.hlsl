struct FArdaInstancingVertexOutput
{
	float4 Position : SV_Position;
	float4 Color : COLOR0;
};

FArdaInstancingVertexOutput InstancingVS(uint Vertex: SV_VertexID, float4 Data: INSTANCE)
{
	const float2 Positions[3] = { float2(-0.35, -0.6), float2(0.35, -0.6), float2(0.0, 0.6) };
	FArdaInstancingVertexOutput Result;
	Result.Position = float4(Positions[Vertex] + Data.xy, 0.5, 1.0);
	Result.Color = float4(1.0 - Data.z, Data.z, 0.0, 1.0);
	return Result;
}

struct FArdaInstancingTint
{
	uint Value;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<FArdaInstancingTint> Tint : register(b0);

float4 InstancingPS(FArdaInstancingVertexOutput Input)
    : SV_Target0
{
	return Input.Color * (float(Tint.Value) / 255.0);
}
