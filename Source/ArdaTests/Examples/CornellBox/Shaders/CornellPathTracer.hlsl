static const float kPi = 3.14159265358979323846;
static const float kRayEpsilon = 0.0015;

struct CornellVertex
{
    float3 Position;
    float Padding;
    float3 Normal;
    uint MaterialId;
};

struct CornellMaterial
{
    float3 BaseColor;
    float Roughness;
    float3 Emission;
    float Metallic;
    float Transmission;
    float Ior;
    float2 Padding;
};

struct CornellPayload
{
    float3 Normal;
    float T;
    uint MaterialId;
    uint Hit;
    uint FrontFace;
    uint Padding;
};

RaytracingAccelerationStructure Scene : register(t0);
StructuredBuffer<CornellVertex> Vertices : register(t1);
StructuredBuffer<uint> Indices : register(t2);
StructuredBuffer<CornellMaterial> Materials : register(t3);
RWStructuredBuffer<float4> SampleRadiance : register(u0);

cbuffer FrameConstants : register(b0)
{
    float4 CameraPositionAndTanHalfFovX;
    float4 CameraForwardAndTanHalfFovY;
    float4 CameraRightAndExposure;
    float4 CameraUpAndLightArea;
    uint4 ImageAndSampling;
    uint4 PathAndSeed;
};

uint NextRandom(inout uint State)
{
    State = State * 747796405u + 2891336453u;
    uint Word = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}

float RandomFloat(inout uint State)
{
    return float(NextRandom(State) >> 8) * (1.0 / 16777216.0);
}

uint HashSeed(uint3 Value)
{
    uint State = Value.x * 0x9e3779b9u ^ Value.y * 0x85ebca6bu ^
        Value.z * 0xc2b2ae35u ^ PathAndSeed.y;
    State ^= State >> 16;
    State *= 0x7feb352du;
    State ^= State >> 15;
    State *= 0x846ca68bu;
    return State ^ (State >> 16);
}

void BuildBasis(float3 Normal, out float3 Tangent, out float3 Bitangent)
{
    const float Sign = Normal.z >= 0.0 ? 1.0 : -1.0;
    const float A = -1.0 / (Sign + Normal.z);
    const float B = Normal.x * Normal.y * A;
    Tangent = float3(1.0 + Sign * Normal.x * Normal.x * A,
        Sign * B, -Sign * Normal.x);
    Bitangent = float3(B, Sign + Normal.y * Normal.y * A, -Normal.y);
}

float3 ToWorld(float3 Local, float3 Normal)
{
    float3 Tangent;
    float3 Bitangent;
    BuildBasis(Normal, Tangent, Bitangent);
    return normalize(Local.x * Tangent + Local.y * Bitangent + Local.z * Normal);
}

float PowerHeuristic(float A, float B)
{
    const float A2 = A * A;
    const float B2 = B * B;
    return A2 / max(A2 + B2, 1.0e-8);
}

float3 FresnelSchlick(float CosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - CosTheta), 5.0);
}

float FresnelDielectric(float CosTheta, float EtaI, float EtaT)
{
    CosTheta = saturate(CosTheta);
    const float SinThetaI = sqrt(max(0.0, 1.0 - CosTheta * CosTheta));
    const float SinThetaT = EtaI / EtaT * SinThetaI;
    if (SinThetaT >= 1.0)
        return 1.0;
    const float CosThetaT = sqrt(max(0.0, 1.0 - SinThetaT * SinThetaT));
    const float Rs = (EtaI * CosTheta - EtaT * CosThetaT) /
        (EtaI * CosTheta + EtaT * CosThetaT);
    const float Rp = (EtaT * CosTheta - EtaI * CosThetaT) /
        (EtaT * CosTheta + EtaI * CosThetaT);
    return 0.5 * (Rs * Rs + Rp * Rp);
}

float GGXDistribution(float NdotH, float Alpha)
{
    const float A2 = Alpha * Alpha;
    const float Denominator = NdotH * NdotH * (A2 - 1.0) + 1.0;
    return A2 / max(kPi * Denominator * Denominator, 1.0e-8);
}

float SmithMask(float NdotX, float Alpha)
{
    const float K = (Alpha + 1.0) * (Alpha + 1.0) * 0.125;
    return NdotX / max(NdotX * (1.0 - K) + K, 1.0e-6);
}

void EvaluateBsdf(
    CornellMaterial Material,
    float3 Normal,
    float3 View,
    float3 Light,
    out float3 F,
    out float Pdf)
{
    const float NdotV = saturate(dot(Normal, View));
    const float NdotL = saturate(dot(Normal, Light));
    F = 0.0;
    Pdf = 0.0;
    if (NdotV <= 0.0 || NdotL <= 0.0 || Material.Transmission > 0.5)
        return;

    const float DiffuseWeight = 1.0 - Material.Metallic;
    F += Material.BaseColor * (DiffuseWeight / kPi);
    Pdf += DiffuseWeight * (NdotL / kPi);

    if (Material.Metallic > 0.0)
    {
        const float3 HalfVector = normalize(View + Light);
        const float NdotH = saturate(dot(Normal, HalfVector));
        const float VdotH = saturate(dot(View, HalfVector));
        const float Alpha = max(Material.Roughness * Material.Roughness, 0.02);
        const float D = GGXDistribution(NdotH, Alpha);
        const float G = SmithMask(NdotV, Alpha) * SmithMask(NdotL, Alpha);
        const float3 Fresnel = FresnelSchlick(VdotH, Material.BaseColor);
        F += Material.Metallic * D * G * Fresnel /
            max(4.0 * NdotV * NdotL, 1.0e-6);
        Pdf += Material.Metallic * D * NdotH / max(4.0 * VdotH, 1.0e-6);
    }
}

bool SampleBsdf(
    CornellMaterial Material,
    float3 Normal,
    float3 IncomingDirection,
    bool FrontFace,
    inout uint Rng,
    out float3 Direction,
    out float3 Weight,
    out float Pdf,
    out bool Delta)
{
    const float3 View = -IncomingDirection;
    Delta = false;
    Pdf = 0.0;
    Weight = 0.0;

    if (Material.Transmission > 0.5)
    {
        const float EtaI = FrontFace ? 1.0 : Material.Ior;
        const float EtaT = FrontFace ? Material.Ior : 1.0;
        const float Fresnel = FresnelDielectric(
            saturate(dot(View, Normal)), EtaI, EtaT);
        if (RandomFloat(Rng) < Fresnel)
        {
            Direction = normalize(reflect(IncomingDirection, Normal));
            Weight = 1.0;
        }
        else
        {
            Direction = refract(IncomingDirection, Normal, EtaI / EtaT);
            if (dot(Direction, Direction) < 1.0e-8)
                Direction = reflect(IncomingDirection, Normal);
            Direction = normalize(Direction);
            Weight = Material.BaseColor;
        }
        Delta = true;
        return true;
    }

    const float Choose = RandomFloat(Rng);
    if (Choose < Material.Metallic)
    {
        const float U1 = RandomFloat(Rng);
        const float U2 = RandomFloat(Rng);
        const float Alpha = max(Material.Roughness * Material.Roughness, 0.02);
        const float A2 = Alpha * Alpha;
        const float Phi = 2.0 * kPi * U1;
        const float CosTheta = sqrt((1.0 - U2) /
            max(1.0 + (A2 - 1.0) * U2, 1.0e-6));
        const float SinTheta = sqrt(max(0.0, 1.0 - CosTheta * CosTheta));
        const float3 HalfVector = ToWorld(
            float3(cos(Phi) * SinTheta, sin(Phi) * SinTheta, CosTheta),
            Normal);
        Direction = normalize(reflect(IncomingDirection, HalfVector));
    }
    else
    {
        const float U1 = RandomFloat(Rng);
        const float U2 = RandomFloat(Rng);
        const float Radius = sqrt(U1);
        const float Phi = 2.0 * kPi * U2;
        Direction = ToWorld(float3(
            Radius * cos(Phi), Radius * sin(Phi), sqrt(1.0 - U1)), Normal);
    }

    const float Cosine = saturate(dot(Normal, Direction));
    if (Cosine <= 0.0)
        return false;
    float3 F;
    EvaluateBsdf(Material, Normal, View, Direction, F, Pdf);
    if (Pdf <= 1.0e-7)
        return false;
    Weight = F * Cosine / Pdf;
    return all(isfinite(Weight));
}

bool IsVisible(float3 Origin, float3 Direction, float Distance)
{
    RayDesc Ray;
    Ray.Origin = Origin;
    Ray.Direction = Direction;
    Ray.TMin = kRayEpsilon;
    Ray.TMax = max(Distance - kRayEpsilon * 2.0, kRayEpsilon);
    CornellPayload Payload;
    Payload.Normal = 0.0;
    Payload.T = 0.0;
    Payload.MaterialId = 0;
    Payload.Hit = 1;
    Payload.FrontFace = 1;
    Payload.Padding = 0;
    TraceRay(Scene,
        RAY_FLAG_FORCE_OPAQUE |
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
        RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        0xff, 0, 0, 0, Ray, Payload);
    return Payload.Hit == 0;
}

float3 TracePath(float3 Origin, float3 Direction, inout uint Rng)
{
    float3 Radiance = 0.0;
    float3 Throughput = 1.0;
    float LastBsdfPdf = 0.0;
    bool LastDelta = true;

    [loop]
    for (uint Bounce = 0; Bounce < PathAndSeed.x; ++Bounce)
    {
        RayDesc Ray;
        Ray.Origin = Origin;
        Ray.Direction = Direction;
        Ray.TMin = kRayEpsilon;
        Ray.TMax = 10000.0;
        CornellPayload Payload;
        Payload.Normal = 0.0;
        Payload.T = 0.0;
        Payload.MaterialId = 0;
        Payload.Hit = 0;
        Payload.FrontFace = 1;
        Payload.Padding = 0;
        TraceRay(Scene, RAY_FLAG_FORCE_OPAQUE,
            0xff, 0, 0, 0, Ray, Payload);
        if (Payload.Hit == 0)
            break;

        const float3 Position = Origin + Direction * Payload.T;
        const float3 Normal = normalize(Payload.Normal);
        const CornellMaterial Material = Materials[Payload.MaterialId];
        const float EmissionStrength = max(
            Material.Emission.x, max(Material.Emission.y, Material.Emission.z));
        if (EmissionStrength > 0.0)
        {
            float Weight = 1.0;
            if (Bounce > 0 && !LastDelta)
            {
                const float CosLight = max(dot(float3(0,0,-1), -Direction), 1.0e-6);
                const float LightPdf = Payload.T * Payload.T /
                    max(CameraUpAndLightArea.w * CosLight, 1.0e-6);
                Weight = PowerHeuristic(LastBsdfPdf, LightPdf);
            }
            Radiance += Throughput * Material.Emission * Weight;
            break;
        }

        if (Material.Transmission <= 0.5)
        {
            const float2 LightSample = float2(RandomFloat(Rng), RandomFloat(Rng));
            const float3 LightPosition = float3(
                lerp(-0.35, 0.35, LightSample.x),
                lerp(0.75, 1.35, LightSample.y), 1.99);
            const float3 ToLight = LightPosition - Position;
            const float DistanceSquared = dot(ToLight, ToLight);
            const float Distance = sqrt(DistanceSquared);
            const float3 LightDirection = ToLight / max(Distance, 1.0e-6);
            const float NdotL = saturate(dot(Normal, LightDirection));
            const float CosLight = saturate(dot(float3(0,0,-1), -LightDirection));
            if (NdotL > 0.0 && CosLight > 0.0 &&
                IsVisible(Position + Normal * kRayEpsilon,
                    LightDirection, Distance))
            {
                float3 F;
                float BsdfPdf;
                EvaluateBsdf(Material, Normal, -Direction,
                    LightDirection, F, BsdfPdf);
                const float LightPdf = DistanceSquared /
                    max(CameraUpAndLightArea.w * CosLight, 1.0e-6);
                const float Mis = PowerHeuristic(LightPdf, BsdfPdf);
                Radiance += Throughput * Materials[6].Emission *
                    F * NdotL * Mis / max(LightPdf, 1.0e-6);
            }
        }

        float3 NewDirection;
        float3 BsdfWeight;
        float BsdfPdf;
        bool Delta;
        if (!SampleBsdf(Material, Normal, Direction,
                Payload.FrontFace != 0, Rng,
                NewDirection, BsdfWeight, BsdfPdf, Delta))
            break;
        Throughput *= BsdfWeight;
        LastBsdfPdf = BsdfPdf;
        LastDelta = Delta;

        if (Bounce >= 3)
        {
            const float ContinueProbability =
                clamp(max(Throughput.x, max(Throughput.y, Throughput.z)), 0.05, 0.95);
            if (RandomFloat(Rng) > ContinueProbability)
                break;
            Throughput /= ContinueProbability;
        }
        Origin = Position + NewDirection * kRayEpsilon;
        Direction = NewDirection;
    }
    return Radiance;
}

[shader("raygeneration")]
void CornellRayGen()
{
    const uint3 LaunchIndex = DispatchRaysIndex();
    const uint2 Pixel = LaunchIndex.xy;
    if (any(Pixel >= ImageAndSampling.xy) ||
        LaunchIndex.z >= ImageAndSampling.w)
        return;

    const uint GlobalSample = ImageAndSampling.z + LaunchIndex.z;
    uint Rng = HashSeed(uint3(
        Pixel, GlobalSample + PathAndSeed.z * 65537u));
    const float2 Jitter = float2(RandomFloat(Rng), RandomFloat(Rng));
    const float2 Uv =
        (float2(Pixel) + Jitter) / float2(ImageAndSampling.xy);
    const float2 Ndc = float2(Uv.x * 2.0 - 1.0, 1.0 - Uv.y * 2.0);
    const float3 RayDirection = normalize(
        CameraForwardAndTanHalfFovY.xyz +
        CameraRightAndExposure.xyz *
            (Ndc.x * CameraPositionAndTanHalfFovX.w) +
        CameraUpAndLightArea.xyz *
            (Ndc.y * CameraForwardAndTanHalfFovY.w));
    const float3 Radiance = TracePath(
        CameraPositionAndTanHalfFovX.xyz, RayDirection, Rng);
    const uint PixelIndex = Pixel.y * ImageAndSampling.x + Pixel.x;
    const uint SampleStride = ImageAndSampling.x * ImageAndSampling.y;
    SampleRadiance[LaunchIndex.z * SampleStride + PixelIndex] =
        float4(Radiance, 1.0);
}

[shader("miss")]
void CornellMiss(inout CornellPayload Payload)
{
    Payload.Hit = 0;
}

[shader("closesthit")]
void CornellClosestHit(
    inout CornellPayload Payload,
    BuiltInTriangleIntersectionAttributes Attributes)
{
    const uint Primitive = PrimitiveIndex();
    const uint I0 = Indices[Primitive * 3 + 0];
    const uint I1 = Indices[Primitive * 3 + 1];
    const uint I2 = Indices[Primitive * 3 + 2];
    const CornellVertex V0 = Vertices[I0];
    const CornellVertex V1 = Vertices[I1];
    const CornellVertex V2 = Vertices[I2];
    const float3 Barycentrics = float3(
        1.0 - Attributes.barycentrics.x - Attributes.barycentrics.y,
        Attributes.barycentrics.x,
        Attributes.barycentrics.y);
    float3 Normal = normalize(
        V0.Normal * Barycentrics.x +
        V1.Normal * Barycentrics.y +
        V2.Normal * Barycentrics.z);
    const bool FrontFace = dot(Normal, WorldRayDirection()) < 0.0;
    if (!FrontFace)
        Normal = -Normal;
    Payload.Normal = Normal;
    Payload.T = RayTCurrent();
    Payload.MaterialId = V0.MaterialId;
    Payload.Hit = 1;
    Payload.FrontFace = FrontFace ? 1 : 0;
}
