StructuredBuffer<uint> Input : register(t0);
RWByteAddressBuffer Output : register(u0);

[numthreads(4, 1, 1)]
void UsageCS(uint3 Index: SV_DispatchThreadID)
{
	Output.Store(Index.x * 4, Input[Index.x] * 3 + 7);
}
