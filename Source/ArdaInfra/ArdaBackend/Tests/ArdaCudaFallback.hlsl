StructuredBuffer<uint> Input : register(t0);
RWStructuredBuffer<uint> Output : register(u0);

struct FArdaAlternativeConstants
{
	uint mBias;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<FArdaAlternativeConstants> Constants : register(b0);

[numthreads(128, 1, 1)]
void AddFallbackCS(uint3 Thread: SV_DispatchThreadID)
{
	uint Count, Stride;
	Output.GetDimensions(Count, Stride);
	if (Thread.x < Count)
	{
		Output[Thread.x] = Input[Thread.x] + Constants.mBias;
	}
}
