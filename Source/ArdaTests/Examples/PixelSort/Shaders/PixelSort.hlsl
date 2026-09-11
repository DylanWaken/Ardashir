cbuffer Frame : register(b0) { uint Width; uint Height; float Time; uint ShowOriginal; }
#ifdef __spirv__
[[vk::image_format("rgba8ui")]]
#endif
RWTexture2D<uint4> NoiseOutput : register(u0);
Texture2D<uint4> Sorted : register(t0);
Texture2D<uint4> Original : register(t1);

float Hash(float2 P) { return frac(sin(dot(P, float2(127.1, 311.7))) * 43758.5453); }
float Noise(float2 P)
{
    float2 I = floor(P), F = frac(P); F = F * F * (3.0 - 2.0 * F);
    return lerp(lerp(Hash(I), Hash(I + float2(1, 0)), F.x),
        lerp(Hash(I + float2(0, 1)), Hash(I + 1), F.x), F.y);
}
float Fbm(float2 P)
{
    float Sum = 0, Gain = 0.5;
    for (uint I = 0; I < 5; ++I) { Sum += Gain * Noise(P); P = mul(float2x2(1.6, -1.2, 1.2, 1.6), P) + 2.31; Gain *= 0.5; }
    return Sum;
}
float3 Palette(float T)
{
    float3 Ink = float3(0.075, 0.082, 0.13), Indigo = float3(0.24, 0.29, 0.56);
    float3 Lavender = float3(0.56, 0.59, 0.92), Coral = float3(0.94, 0.39, 0.48);
    float3 C = lerp(Ink, Indigo, smoothstep(0.19, 0.43, T));
    C = lerp(C, Lavender, smoothstep(0.40, 0.64, T));
    return lerp(C, Coral, smoothstep(0.64, 0.80, T));
}
[numthreads(8, 8, 1)]
void NoiseCS(uint3 ID : SV_DispatchThreadID)
{
    if (ID.x >= Width || ID.y >= Height) return;
    float2 UV = (float2(ID.xy) + 0.5) / float2(Width, Height);
    float2 P = (UV - 0.5) * float2(float(Width) / Height, 1) * 4.4;
    float2 W = float2(Fbm(P + float2(Time * 0.045, 3.2)), Fbm(P + float2(5.7, -Time * 0.038)));
    float Field = Fbm(P + 2.8 * W + float2(0.03, -0.025) * Time);
    float Ridges = sin((P.x * 1.2 + P.y + W.x * 1.8) * 31.0 + Time * 0.13);
    float T = saturate((Field - 0.23) * 1.8 + Ridges * 0.035);
    float3 Color = Palette(T);
    Color *= lerp(0.48, 1.0, smoothstep(0.26, 0.44, Field));
    Color += (Hash(float2(ID.xy)) - 0.5) * 0.008;
    NoiseOutput[ID.xy] = uint4(round(saturate(Color) * 255), 255);
}
float4 PresentVS(uint ID : SV_VertexID) : SV_Position
{
    float2 P = float2((ID << 1) & 2, ID & 2);
    return float4(P * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 PresentPS(float4 Position : SV_Position) : SV_Target0
{
    uint2 Pixel = min(uint2(Position.xy), uint2(Width - 1, Height - 1));
    return float4(ShowOriginal ? Original.Load(int3(Pixel, 0)) : Sorted.Load(int3(Pixel, 0))) / 255.0;
}
