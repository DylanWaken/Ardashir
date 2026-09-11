struct TerrainSettings
{
    uint mWidth;
    uint mHeight;
    float mFrequency;
    float mAmplitude;
    float mTime;
    float3 mPadding;
};

struct TerrainVertex
{
    float3 mPosition;
    float mHeight;
};

float Hash(float3 value)
{
    // ValueNoise calls Hash only with integral lattice coordinates. Keep the
    // hash entirely in uint arithmetic so DXIL and SPIR-V cannot diverge
    // through implementation-dependent trigonometric approximations.
    const uint3 lattice = asuint(int3(value));
    uint hash = 2166136261u;
    hash = (hash ^ lattice.x) * 16777619u;
    hash = (hash ^ lattice.y) * 16777619u;
    hash = (hash ^ lattice.z) * 16777619u;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;
    return float(hash >> 8) * (1.0 / 16777216.0);
}

float ValueNoise(float3 value)
{
    const float3 cell = floor(value);
    const float3 fraction = frac(value);
    const float3 blend =
        fraction * fraction * (3.0 - 2.0 * fraction);

    const float c000 = Hash(cell);
    const float c100 = Hash(cell + float3(1.0, 0.0, 0.0));
    const float c010 = Hash(cell + float3(0.0, 1.0, 0.0));
    const float c110 = Hash(cell + float3(1.0, 1.0, 0.0));
    const float c001 = Hash(cell + float3(0.0, 0.0, 1.0));
    const float c101 = Hash(cell + float3(1.0, 0.0, 1.0));
    const float c011 = Hash(cell + float3(0.0, 1.0, 1.0));
    const float c111 = Hash(cell + float3(1.0, 1.0, 1.0));

    const float lower = lerp(
        lerp(c000, c100, blend.x),
        lerp(c010, c110, blend.x),
        blend.y);
    const float upper = lerp(
        lerp(c001, c101, blend.x),
        lerp(c011, c111, blend.x),
        blend.y);
    return lerp(lower, upper, blend.z);
}

StructuredBuffer<TerrainSettings> GenerateSettings : register(t0);
RWTexture2D<float> GenerateHeightmap : register(u0);

[numthreads(8, 8, 1)]
void GenerateNoiseHeightmapCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const TerrainSettings settings = GenerateSettings[0];
    if (dispatchThreadId.x >= settings.mWidth ||
        dispatchThreadId.y >= settings.mHeight)
    {
        return;
    }

    const float2 uv =
        float2(dispatchThreadId.xy) /
        float2(settings.mWidth - 1, settings.mHeight - 1);
    float noise = 0.0;
    float weight = 0.55;
    float frequency = settings.mFrequency;
    float totalWeight = 0.0;
    [unroll]
    for (uint octave = 0; octave < 3; ++octave)
    {
        const float timeCoordinate =
            settings.mTime * 0.22 + float(octave) * 19.0;
        noise += ValueNoise(float3(uv * frequency, timeCoordinate)) * weight;
        totalWeight += weight;
        frequency *= 2.03;
        weight *= 0.5;
    }

    const float boundaryHeight = 0.42;
    const float signedNoise = noise / totalWeight * 2.0 - 1.0;
    GenerateHeightmap[dispatchThreadId.xy] =
        boundaryHeight +
        signedNoise * settings.mAmplitude * 0.55;
}

RWTexture2D<float> ErosionHeightmap : register(u0);

[numthreads(8, 8, 1)]
void ErodeHeightmapCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width;
    uint height;
    ErosionHeightmap.GetDimensions(width, height);
    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
    {
        return;
    }

    const float original = ErosionHeightmap[dispatchThreadId.xy];
    const float sediment = smoothstep(0.48, 0.9, original) * 0.065;
    const float erodedHeight = original - sediment;

    const float2 uv =
        float2(dispatchThreadId.xy) / float2(width - 1, height - 1);
    const float edgeDistance = min(
        min(uv.x, 1.0 - uv.x),
        min(uv.y, 1.0 - uv.y));
    const float boundaryBlend = smoothstep(0.0, 0.12, edgeDistance);
    const float boundaryHeight = 0.42;
    ErosionHeightmap[dispatchThreadId.xy] =
        lerp(boundaryHeight, erodedHeight, boundaryBlend);
}

Texture2D<float> TriangulationHeightmap : register(t0);
RWStructuredBuffer<TerrainVertex> TerrainVertices : register(u0);
RWStructuredBuffer<uint> TerrainIndices : register(u1);

[numthreads(8, 8, 1)]
void TriangulateTerrainCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width;
    uint height;
    TriangulationHeightmap.GetDimensions(width, height);
    if (dispatchThreadId.x >= width - 1 ||
        dispatchThreadId.y >= height - 1)
    {
        return;
    }

    const uint cell = dispatchThreadId.y * (width - 1) + dispatchThreadId.x;
    const uint vertexBase = cell * 4;
    const uint indexBase = cell * 6;
    const uint2 coordinates[4] = {
        dispatchThreadId.xy,
        dispatchThreadId.xy + uint2(1, 0),
        dispatchThreadId.xy + uint2(0, 1),
        dispatchThreadId.xy + uint2(1, 1)
    };

    [unroll]
    for (uint corner = 0; corner < 4; ++corner)
    {
        const float sampleHeight =
            TriangulationHeightmap.Load(int3(coordinates[corner], 0));
        const float2 uv =
            float2(coordinates[corner]) / float2(width - 1, height - 1);
        TerrainVertex vertex;
        vertex.mPosition = float3(
            (uv.y - 0.5) * 1.45,
            (uv.x - 0.5) * 1.45,
            sampleHeight * 0.72 - 0.32);
        vertex.mHeight = sampleHeight;
        TerrainVertices[vertexBase + corner] = vertex;
    }

    TerrainIndices[indexBase + 0] = vertexBase + 0;
    TerrainIndices[indexBase + 1] = vertexBase + 2;
    TerrainIndices[indexBase + 2] = vertexBase + 1;
    TerrainIndices[indexBase + 3] = vertexBase + 1;
    TerrainIndices[indexBase + 4] = vertexBase + 2;
    TerrainIndices[indexBase + 5] = vertexBase + 3;
}

struct TerrainVertexInput
{
    float3 mPosition : POSITION;
    float mHeight : HEIGHT;
};

struct TerrainVertexOutput
{
    float4 mPosition : SV_Position;
    float2 mUV : TEXCOORD0;
    float mDepth : DEPTH;
};

cbuffer CameraSettings : register(b0)
{
    row_major float4x4 WorldToView;
    row_major float4x4 Projection;
};

TerrainVertexOutput TerrainVS(TerrainVertexInput input)
{
    TerrainVertexOutput output;
    const float4 viewPosition =
        mul(float4(input.mPosition, 1.0), WorldToView);
    output.mPosition = mul(viewPosition, Projection);
    output.mUV =
        float2(input.mPosition.y, input.mPosition.x) / 1.45 + 0.5;
    output.mDepth = viewPosition.z;
    return output;
}

Texture2D<float> RenderHeightmap : register(t0);

float SampleHeightmap(float2 uv)
{
    uint width;
    uint height;
    RenderHeightmap.GetDimensions(width, height);

    const float2 grid =
        saturate(uv) * float2(width - 1, height - 1);
    const uint2 lower = uint2(floor(grid));
    const uint2 upper = min(lower + 1, uint2(width - 1, height - 1));
    const float2 blend = frac(grid);
    const float lowerHeight = lerp(
        RenderHeightmap.Load(int3(lower, 0)),
        RenderHeightmap.Load(int3(uint2(upper.x, lower.y), 0)),
        blend.x);
    const float upperHeight = lerp(
        RenderHeightmap.Load(int3(uint2(lower.x, upper.y), 0)),
        RenderHeightmap.Load(int3(upper, 0)),
        blend.x);
    return lerp(lowerHeight, upperHeight, blend.y);
}

float ContourMask(float height, float interval, float thickness)
{
    const float coordinate = height / interval;
    const float distanceToLine =
        abs(frac(coordinate + 0.5) - 0.5);
    const float filterWidth = max(fwidth(coordinate), 0.0001);
    return 1.0 - smoothstep(
        filterWidth * thickness,
        filterWidth * (thickness + 1.0),
        distanceToLine);
}

float4 TerrainPS(TerrainVertexOutput input) : SV_Target
{
    const float height = SampleHeightmap(input.mUV);
    const float minorContour = ContourMask(height, 0.010, 0.48);
    const float majorContour = ContourMask(height, 0.050, 0.78);

    const float boundaryHeight = 0.42;
    const float relativeHeight = height - boundaryHeight;
    const float distanceFromBoundary = abs(relativeHeight);
    const float boundaryFade = lerp(
        0.3,
        1.0,
        smoothstep(0.0, 0.055, distanceFromBoundary));
    const float elevation = saturate(distanceFromBoundary / 0.28);

    const float3 aboveColor = lerp(
        float3(0.72, 0.72, 0.72),
        float3(1.0, 1.0, 1.0),
        elevation);
    const float3 belowColor = lerp(
        float3(0.72, 0.015, 0.01),
        float3(1.0, 0.025, 0.015),
        elevation);
    const float aboveBlend = smoothstep(
        -0.055,
        0.055,
        relativeHeight);
    const float3 contourColor = lerp(
        belowColor,
        aboveColor,
        aboveBlend);

    const float3 terrainColor =
        contourColor * (0.012 + elevation * 0.018) * boundaryFade;
    const float contour = max(minorContour * 0.72, majorContour);
    float3 color = lerp(
        terrainColor,
        contourColor,
        contour * boundaryFade);
    color *= 1.0 - saturate(input.mDepth * 0.06) * 0.12;
    return float4(color, 1.0);
}

struct OverlayVertexOutput
{
    float4 mPosition : SV_Position;
};

OverlayVertexOutput TerrainOverlayVS(uint vertexId : SV_VertexID)
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0)
    };
    OverlayVertexOutput output;
    output.mPosition = float4(positions[vertexId], 0.0, 1.0);
    return output;
}

float4 TerrainOverlayPS(OverlayVertexOutput input) : SV_Target
{
    const float pulse = 0.5 + 0.5 * sin(input.mPosition.y * 0.025);
    return float4(0.01, 0.018 + pulse * 0.006, 0.015, 0.08);
}
