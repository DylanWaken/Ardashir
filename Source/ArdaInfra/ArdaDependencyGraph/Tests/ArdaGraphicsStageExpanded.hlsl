#include "ArdaGraphicsStageCapture.hlsl"

[maxvertexcount(3)]
void StageGS(triangle FArdaVertexOutput Input[3], inout TriangleStream<FArdaVertexOutput> Output)
{
	for (uint Index = 0; Index < 3; ++Index)
	{
		FArdaVertexOutput Vertex = Input[Index];
		Vertex.Color = float4(0, 0, 1, 1);
		Output.Append(Vertex);
	}
	Output.RestartStrip();
}

struct FArdaTessellationFactors
{
	float Edges[3] : SV_TessFactor;
	float Inside : SV_InsideTessFactor;
};

FArdaTessellationFactors StagePatchConstants(InputPatch<FArdaVertexOutput, 3> Input)
{
	FArdaTessellationFactors Factors;
	Factors.Edges[0] = Factors.Edges[1] = Factors.Edges[2] = Factors.Inside = 2;
	return Factors;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("StagePatchConstants")]
FArdaVertexOutput StageHS(InputPatch<FArdaVertexOutput, 3> Input, uint ControlPoint: SV_OutputControlPointID)
{
	FArdaVertexOutput Vertex = Input[ControlPoint];
	Vertex.Color = float4(0, 1, 0, 1);
	return Vertex;
}

[domain("tri")]
FArdaVertexOutput StageDS(FArdaTessellationFactors Factors,
    float3 Barycentric: SV_DomainLocation,
    const OutputPatch<FArdaVertexOutput, 3> Patch)
{
	FArdaVertexOutput Vertex;
	Vertex.Position =
	    Patch[0].Position * Barycentric.x + Patch[1].Position * Barycentric.y + Patch[2].Position * Barycentric.z;
	Vertex.Color = Patch[0].Color;
	// Cyan proves the HS output reached DS, and the DS output reached PS.
	Vertex.Color.b = 1;
	return Vertex;
}

struct FArdaMeshPayload
{
	float Gain;
};

groupshared FArdaMeshPayload Payload;

[numthreads(1, 1, 1)]
void StageAS()
{
	Payload.Gain = InputVertices[0].Color.w;
	DispatchMesh(1, 1, 1, Payload);
}

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void StageMS(out vertices FArdaVertexOutput Vertices[3], out indices uint3 Triangles[1])
{
	SetMeshOutputCounts(3, 1);
	for (uint Index = 0; Index < 3; ++Index)
	{
		FArdaVertex Value = TransformVertex(InputVertices[Index]);
		Vertices[Index].Position = Value.Position;
		Vertices[Index].Color = Value.Color;
	}
	Triangles[0] = uint3(0, 1, 2);
}

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void StagePayloadMS(in payload FArdaMeshPayload Input,
    out vertices FArdaVertexOutput Vertices[3],
    out indices uint3 Triangles[1])
{
	SetMeshOutputCounts(3, 1);
	for (uint Index = 0; Index < 3; ++Index)
	{
		FArdaVertex Value = TransformVertex(InputVertices[Index]);
		Vertices[Index].Position = Value.Position;
		Vertices[Index].Color = Value.Color * Input.Gain;
	}
	Triangles[0] = uint3(0, 1, 2);
}
