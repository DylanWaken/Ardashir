cbuffer Matrices : register(b0)
{
	row_major float4x4 ModelToClip;
	row_major float3x3 NormalToWorld;
	row_major float4x4 WorldToView;
	row_major float4x4 ViewToClip;
	float4 Position;
	float4 Normal;
	float4 ViewDepths;
};

RWStructuredBuffer<float4> Output : register(u0);

float ProjectDeviceZ(float ViewZ)
{
	const float4 Clip = mul(ViewToClip, float4(0, 0, ViewZ, 1));
	return Clip.z / Clip.w;
}

[numthreads(1, 1, 1)]
void MatrixInteropCS()
{
	Output[0] = mul(ModelToClip, Position);
	Output[1] = float4(normalize(mul(NormalToWorld, Normal.xyz)), 0);
	Output[2] = mul(ViewToClip, mul(WorldToView, Position));
	Output[3] =
	    float4(ProjectDeviceZ(ViewDepths.x), ProjectDeviceZ(ViewDepths.y), ProjectDeviceZ(ViewDepths.z), ViewDepths.w);
}
