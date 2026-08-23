StructuredBuffer<float4> SampleRadiance : register(t0);
RWTexture2D<float4> Accumulation : register(u0);

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
void CornellAccumulateCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    const uint2 Pixel = DispatchThreadId.xy;
    if (any(Pixel >= ImageAndSampling.xy) || ImageAndSampling.w == 0)
        return;

    const uint PixelIndex = Pixel.y * ImageAndSampling.x + Pixel.x;
    const uint SampleStride = ImageAndSampling.x * ImageAndSampling.y;
    float3 BatchRadiance = 0.0;
    [loop]
    for (uint Sample = 0; Sample < ImageAndSampling.w; ++Sample)
    {
        BatchRadiance +=
            SampleRadiance[Sample * SampleStride + PixelIndex].rgb;
    }

    const float PreviousSampleCount = float(ImageAndSampling.z);
    const float TotalSampleCount =
        PreviousSampleCount + float(ImageAndSampling.w);
    const float3 Previous = ImageAndSampling.z == 0 ?
        0.0 : Accumulation[Pixel].rgb;
    const float3 Average =
        (Previous * PreviousSampleCount + BatchRadiance) /
        max(TotalSampleCount, 1.0);
    Accumulation[Pixel] = float4(Average, 1.0);
}
