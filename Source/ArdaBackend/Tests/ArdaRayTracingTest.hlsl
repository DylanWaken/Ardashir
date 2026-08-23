RWStructuredBuffer<uint> RayOutput : register(u0);
RaytracingAccelerationStructure RayScene : register(t0);

struct FKnownRayPayload
{
    uint Value;
};

[shader("raygeneration")]
void RayGen()
{
    RayOutput[0] = 0xA11CE;
}

[shader("miss")]
void KnownMiss(inout FKnownRayPayload payload)
{
    payload.Value = 0xB055u;
}

[shader("closesthit")]
void KnownClosestHit(
    inout FKnownRayPayload payload,
    BuiltInTriangleIntersectionAttributes attributes)
{
    payload.Value = 0xC105E57u +
        (attributes.barycentrics.x >= 0.0 ? 0u : 1u);
}

[shader("raygeneration")]
void KnownSceneRayGen()
{
    const uint index = DispatchRaysIndex().x;
    RayDesc ray;
    ray.Origin = float3(index == 0 ? 0.0 : 4.0, 0.0, -1.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.001;
    ray.TMax = 10.0;
    FKnownRayPayload payload;
    payload.Value = 0;
    TraceRay(
        RayScene,
        RAY_FLAG_NONE,
        0xff,
        0,
        1,
        0,
        ray,
        payload);
    RayOutput[index] = payload.Value;
}
