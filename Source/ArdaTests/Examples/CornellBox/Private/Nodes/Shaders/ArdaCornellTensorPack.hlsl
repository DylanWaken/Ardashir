Texture2D<float4> Image : register(t0);
RWStructuredBuffer<float> Tensor : register(u0);

cbuffer FrameConstants : register(b0)
{
	float4 CameraPositionAndTanHalfFovX;
	float4 CameraForwardAndTanHalfFovY;
	float4 CameraRightAndExposure;
	float4 CameraUpAndLightArea;
	uint4 ImageAndSampling;
	uint4 PathAndSeed;
};

[numthreads(8, 8, 1)]
void MainCS(uint3 Thread: SV_DispatchThreadID)
{
	if (any(Thread.xy >= ImageAndSampling.xy))
	{
		return;
	}
	uint Index = Thread.y * ImageAndSampling.x + Thread.x;
	uint Plane = ImageAndSampling.x * ImageAndSampling.y;
	float3 Color = max(Image.Load(int3(Thread.xy, 0)).rgb, 0.0);
	Tensor[Index] = Color.r;
	Tensor[Plane + Index] = Color.g;
	Tensor[2 * Plane + Index] = Color.b;
}
