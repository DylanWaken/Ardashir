Texture2D<float4> Accumulation : register(t0);

cbuffer FrameConstants : register(b0)
{
    float4 CameraPositionAndTanHalfFovX;
    float4 CameraForwardAndTanHalfFovY;
    float4 CameraRightAndExposure;
    float4 CameraUpAndLightArea;
    uint4 ImageAndSampling;
    uint4 PathAndSeed;
};

struct PresentVertexOutput
{
    float4 Position : SV_Position;
};

PresentVertexOutput CornellPresentVS(uint VertexId : SV_VertexID)
{
    const float2 Positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0)
    };
    PresentVertexOutput Output;
    Output.Position = float4(Positions[VertexId], 0.0, 1.0);
    return Output;
}

float3 AcesFitted(float3 Color)
{
    const float A = 2.51;
    const float B = 0.03;
    const float C = 2.43;
    const float D = 0.59;
    const float E = 0.14;
    return saturate((Color * (A * Color + B)) /
        (Color * (C * Color + D) + E));
}

float4 CornellPresentPS(PresentVertexOutput Input) : SV_Target
{
    const int2 Pixel = int2(Input.Position.xy);
    float3 Color = max(Accumulation.Load(int3(Pixel, 0)).rgb, 0.0);
    Color = AcesFitted(Color * CameraRightAndExposure.w);
    Color = pow(Color, 1.0 / 2.2);
    return float4(Color, 1.0);
}
