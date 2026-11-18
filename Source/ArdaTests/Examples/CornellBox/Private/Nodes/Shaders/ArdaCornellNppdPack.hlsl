StructuredBuffer<float4> Radiance : register(t0);
StructuredBuffer<float4> Guides : register(t1);
RWStructuredBuffer<float> Color : register(u0);
RWStructuredBuffer<float> Depth : register(u1);
RWStructuredBuffer<float> Normal : register(u2);
RWStructuredBuffer<float> Diffuse : register(u3);
RWStructuredBuffer<float> Motion : register(u4);
RWStructuredBuffer<float> MeanColor : register(u5);

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
	const uint Pixel = Thread.y * ImageAndSampling.x + Thread.x;
	const uint Plane = ImageAndSampling.x * ImageAndSampling.y;
	float3 Mean = 0.0;
	[unroll]
	for (uint Sample = 0; Sample < 2; ++Sample)
	{
		const uint Source = Sample * Plane + Pixel;
		const float3 Rgb = max(Radiance[Source].rgb, 0.0);
		const float4 Guide = Guides[Source * 2];
		const float3 Albedo = Guides[Source * 2 + 1].rgb;
		// Noisebase's normal convention is camera forward, right, up in this order.
		const float3 CameraNormal = float3(dot(Guide.xyz, CameraForwardAndTanHalfFovY.xyz),
		    dot(Guide.xyz, CameraRightAndExposure.xyz),
		    dot(Guide.xyz, CameraUpAndLightArea.xyz));
		[unroll]
		for (uint Channel = 0; Channel < 3; ++Channel)
		{
			const uint Target = (Channel * Plane + Pixel) * 2 + Sample;
			Color[Target] = Rgb[Channel];
			Normal[Target] = CameraNormal[Channel];
			Diffuse[Target] = Albedo[Channel];
		}
		Depth[Pixel * 2 + Sample] = Guide.w > 0.0 ? log(1.0 + 1.0 / Guide.w) : 0.0;
		// Static scene and stationary camera: exactly zero pixel displacement.
		// The renderer resets temporal history whenever the camera moves.
		Motion[Pixel * 2 + Sample] = 0.0;
		Motion[(Plane + Pixel) * 2 + Sample] = 0.0;
		Mean += Rgb * 0.5;
	}
	MeanColor[Pixel] = Mean.r;
	MeanColor[Plane + Pixel] = Mean.g;
	MeanColor[2 * Plane + Pixel] = Mean.b;
}
