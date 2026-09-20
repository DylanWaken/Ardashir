[shader("raygeneration")]
void EmptyRaygen()
{
}

struct FArdaPayload
{
	uint Value;
};

[shader("miss")]
void EmptyMiss(inout FArdaPayload P)
{
	P.Value = 0;
}

RWStructuredBuffer<uint> LocalOutput : register(u0, space4);

[shader("raygeneration")]
void LocalRaygen()
{
	LocalOutput[0] = 123;
}
