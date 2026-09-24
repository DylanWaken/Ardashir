RWStructuredBuffer<uint> mOutput : register(u0);

[numthreads(64, 1, 1)] void AlternativeCS(uint3 Thread : SV_DispatchThreadID)
{
	uint Count, Stride;
	mOutput.GetDimensions(Count, Stride);
	if (Thread.x < Count)
	{
		mOutput[Thread.x] = Thread.x * 3 + 7;
	}
}
