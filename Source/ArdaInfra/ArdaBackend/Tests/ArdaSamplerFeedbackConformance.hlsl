Texture2D<float4> PairedTexture : register(t0);
SamplerState PointSampler : register(s0);
FeedbackTexture2D<SAMPLER_FEEDBACK_MIN_MIP> Feedback : register(u0);
FeedbackTexture2D<SAMPLER_FEEDBACK_MIP_REGION_USED> RegionFeedback : register(u2);
RWStructuredBuffer<uint> Metadata : register(u1);

float SampleRequestedMip()
{
	uint Width, Height, Levels;
	PairedTexture.GetDimensions(0, Width, Height, Levels);
	Metadata[1] = Width;
	Metadata[2] = Height;
	Metadata[3] = Levels;
	float Lod = (float)Metadata[0];
	Metadata[4] = (uint)round(PairedTexture.SampleLevel(PointSampler, float2(0.25, 0.25), Lod).r * 255.0);
	return Lod;
}

[numthreads(1, 1, 1)]
void SamplerFeedbackCS()
{
	Feedback.WriteSamplerFeedbackLevel(PairedTexture, PointSampler, float2(0.25, 0.25), SampleRequestedMip());
}

[numthreads(1, 1, 1)]
void SamplerFeedbackRegionCS()
{
	RegionFeedback.WriteSamplerFeedbackLevel(PairedTexture, PointSampler, float2(0.25, 0.25), SampleRequestedMip());
}
