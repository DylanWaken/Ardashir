StructuredBuffer<float> Tensor : register(t0);
RWTexture2D<float4> Image : register(u0);

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
	Image[Thread.xy] = float4(Tensor[Index], Tensor[Plane + Index], Tensor[2 * Plane + Index], 1.0);
}
