RWStructuredBuffer<uint> Output : register(u0);

[numthreads(1, 1, 1)]
void ShaderStructTestCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    Output[dispatchThreadId.x] = 0xA2DA;
}
