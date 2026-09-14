struct FArdaVertexInput
{
	float2 mPosition : POSITION;
	float3 mColor : COLOR;
};

struct FArdaVertexOutput
{
	float4 mPosition : SV_Position;
	float3 mColor : COLOR;
};

FArdaVertexOutput VSMain(FArdaVertexInput input)
{
	FArdaVertexOutput output;
	output.mPosition = float4(input.mPosition, 0.0, 1.0);
	output.mColor = input.mColor;
	return output;
}

float4 PSMain(FArdaVertexOutput input)
    : SV_Target
{
	return float4(input.mColor, 1.0);
}
