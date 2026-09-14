struct FArdaVertex
{
	float4 Position : SV_Position;
};

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void RecipeMS(out vertices FArdaVertex V[3], out indices uint3 T[1])
{
	SetMeshOutputCounts(3, 1);
	V[0].Position = float4(-1, -1, 0, 1);
	V[1].Position = float4(-1, 3, 0, 1);
	V[2].Position = float4(3, -1, 0, 1);
	T[0] = uint3(0, 1, 2);
}

float4 RecipePS()
    : SV_Target
{
	return float4(0, 1, 0, 1);
}
