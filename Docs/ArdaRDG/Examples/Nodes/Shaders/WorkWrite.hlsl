struct FArdaInputRecord
{
	uint Value;
};

RWStructuredBuffer<uint> Output : register(u0);

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeDispatchGrid(1, 1, 1)]
[NumThreads(1, 1, 1)]
void RecipeEntry(DispatchNodeInputRecord<FArdaInputRecord> Input)
{
	Output[0] = Input.Get().Value;
}
