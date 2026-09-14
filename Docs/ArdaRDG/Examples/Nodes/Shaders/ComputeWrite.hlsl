RWStructuredBuffer<uint> Output : register(u0);

struct FArdaWriteParameters
{
	uint Value;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<FArdaWriteParameters> Values : register(b0);

[numthreads(1, 1, 1)]
void RecipeCS()
{
	Output[0] = Values.Value;
}
