cbuffer FirstPushBlock : register(b0)
{
	uint FirstValue;
};

cbuffer SecondPushBlock : register(b1)
{
	uint SecondValue;
};

cbuffer ThirdPushBlock : register(b0, space3)
{
	uint ThirdValue;
};

RWStructuredBuffer<uint> Outputs[2] : register(u0);
RWStructuredBuffer<uint> OtherSpaceOutput : register(u0, space3);

[numthreads(1, 1, 1)]
void PushConstantBroadcastCS(uint3 Thread: SV_DispatchThreadID)
{
	// Distinct stores make each block and descriptor-array element observable through readback.
	Outputs[0][0] = FirstValue;
	Outputs[1][0] = SecondValue;
	OtherSpaceOutput[0] = ThirdValue;
}

[numthreads(1, 1, 1)]
void PushConstantReplayCS(uint3 Thread: SV_DispatchThreadID)
{
	Outputs[0][0] = FirstValue;
}
