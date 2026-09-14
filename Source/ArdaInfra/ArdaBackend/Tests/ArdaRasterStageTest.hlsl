// Executable geometry and tessellation capability conformance.
struct FArdaRasterVertex
{
	float4 Position : SV_Position;
	float4 Color : COLOR0;
};

FArdaRasterVertex RasterStageVS(uint VertexId: SV_VertexID)
{
	const float2 Positions[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
	FArdaRasterVertex V;
	V.Position = float4(Positions[VertexId], 0, 1);
	V.Color = float4(1, 0, 0, 1);
	return V;
}

[maxvertexcount(3)]
void RasterStageGS(triangle FArdaRasterVertex Input[3], inout TriangleStream<FArdaRasterVertex> Output)
{
	for (uint I = 0; I < 3; ++I)
	{
		FArdaRasterVertex V = Input[I];
		V.Color = float4(0, 1, 0, 1);
		Output.Append(V);
	}
	Output.RestartStrip();
}

struct FArdaTessellationFactors
{
	float Edges[3] : SV_TessFactor;
	float Inside : SV_InsideTessFactor;
};

FArdaTessellationFactors RasterStagePatchConstants(InputPatch<FArdaRasterVertex, 3> Input)
{
	FArdaTessellationFactors Factors;
	Factors.Edges[0] = Factors.Edges[1] = Factors.Edges[2] = Factors.Inside = 2;
	return Factors;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("RasterStagePatchConstants")]
FArdaRasterVertex RasterStageHS(InputPatch<FArdaRasterVertex, 3> Input, uint ControlPoint: SV_OutputControlPointID)
{
	FArdaRasterVertex V = Input[ControlPoint];
	// The domain shader must receive the hull shader's changed color.
	V.Color = float4(0, 1, 0, 1);
	return V;
}

[domain("tri")]
FArdaRasterVertex RasterStageDS(FArdaTessellationFactors Factors,
    float3 Barycentric: SV_DomainLocation,
    const OutputPatch<FArdaRasterVertex, 3> Patch)
{
	FArdaRasterVertex V;
	V.Position =
	    Patch[0].Position * Barycentric.x + Patch[1].Position * Barycentric.y + Patch[2].Position * Barycentric.z;
	V.Color = Patch[0].Color;
	V.Color.b = 1;
	return V;
}

float4 RasterStagePS(FArdaRasterVertex Input)
    : SV_Target
{
	return Input.Color;
}
