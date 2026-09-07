RWStructuredBuffer<uint> Output : register(u0);

[numthreads(64, 1, 1)]
void SubgroupCS(uint3 Thread : SV_DispatchThreadID)
{
    uint Sum = WaveActiveSum(Thread.x + 1);
    Output[Thread.x * 2] = Sum;
    Output[Thread.x * 2 + 1] = WaveGetLaneCount();
}

[numthreads(1, 1, 1)]
void Float16CS()
{
    float16_t A = (float16_t)asfloat(Output[0]);
    float16_t B = (float16_t)asfloat(Output[1]);
    float16_t Product = A * B;
    Output[2] = asuint((float)Product);
}

[numthreads(1, 1, 1)]
void Int8CS()
{
    Output[2] = dot4add_u8packed(Output[0], Output[1], 7);
    Output[3] = (uint)dot4add_i8packed(Output[0], Output[1], -3);
}

RWStructuredBuffer<uint> RuntimeOutputs[] : register(u0);

[numthreads(1, 1, 1)]
void RuntimeDescriptorsCS(uint3 Thread : SV_DispatchThreadID)
{
    RuntimeOutputs[NonUniformResourceIndex(Thread.x * 2)][0] = 0xA2DA + Thread.x;
}

RaytracingAccelerationStructure Scene : register(t0);

[numthreads(1, 1, 1)]
void InlineRayQueryCS(uint3 Thread : SV_DispatchThreadID)
{
    RayDesc Ray;
    Ray.Origin = float3(Thread.x == 0 ? 0.0 : 4.0, 0.0, -1.0);
    Ray.Direction = float3(0.0, 0.0, 1.0);
    Ray.TMin = 0.001;
    Ray.TMax = 10.0;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> Query;
    Query.TraceRayInline(Scene, RAY_FLAG_NONE, 0xff, Ray);
    while (Query.Proceed()) {}
    Output[Thread.x] = Query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0xC105E57u : 0xB055u;
}
