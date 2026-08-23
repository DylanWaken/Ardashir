static const uint kRoomAndBoxQuadCount = 18;
static const uint kSphereFaceCount = 6;
static const uint kSphereFaceResolution = 16;
static const uint kSphereTrianglesPerFace =
    kSphereFaceResolution * kSphereFaceResolution * 2;
static const uint kSphereTriangleCount =
    kSphereFaceCount * kSphereTrianglesPerFace;
static const uint kTriangleCount =
    kRoomAndBoxQuadCount * 2 + kSphereTriangleCount * 2;

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

RWStructuredBuffer<CornellVertex> Vertices : register(u0);
RWStructuredBuffer<uint> Indices : register(u1);
RWStructuredBuffer<CornellMaterial> Materials : register(u2);

float3 RotateZ(float3 Value, float Angle)
{
    const float S = sin(Angle);
    const float C = cos(Angle);
    return float3(C * Value.x - S * Value.y,
                  S * Value.x + C * Value.y,
                  Value.z);
}

void GetRoomQuad(
    uint Quad,
    out float3 P0,
    out float3 P1,
    out float3 P2,
    out float3 P3,
    out float3 Normal,
    out uint MaterialId)
{
    if (Quad == 0)
    {
        P0 = float3(-1, 0, 0); P1 = float3(1, 0, 0);
        P2 = float3(1, 2, 0); P3 = float3(-1, 2, 0);
        Normal = float3(0, 0, 1); MaterialId = 0;
    }
    else if (Quad == 1)
    {
        P0 = float3(-1, 0, 2); P1 = float3(-1, 2, 2);
        P2 = float3(1, 2, 2); P3 = float3(1, 0, 2);
        Normal = float3(0, 0, -1); MaterialId = 0;
    }
    else if (Quad == 2)
    {
        P0 = float3(-1, 2, 0); P1 = float3(1, 2, 0);
        P2 = float3(1, 2, 2); P3 = float3(-1, 2, 2);
        Normal = float3(0, -1, 0); MaterialId = 0;
    }
    else if (Quad == 3)
    {
        P0 = float3(-1, 0, 0); P1 = float3(-1, 2, 0);
        P2 = float3(-1, 2, 2); P3 = float3(-1, 0, 2);
        Normal = float3(1, 0, 0); MaterialId = 1;
    }
    else if (Quad == 4)
    {
        P0 = float3(1, 2, 0); P1 = float3(1, 0, 0);
        P2 = float3(1, 0, 2); P3 = float3(1, 2, 2);
        Normal = float3(-1, 0, 0); MaterialId = 2;
    }
    else
    {
        P0 = float3(-0.35, 0.75, 1.99);
        P1 = float3(-0.35, 1.35, 1.99);
        P2 = float3(0.35, 1.35, 1.99);
        P3 = float3(0.35, 0.75, 1.99);
        Normal = float3(0, 0, -1); MaterialId = 6;
    }
}

void GetBoxQuad(
    uint Box,
    uint Face,
    out float3 P0,
    out float3 P1,
    out float3 P2,
    out float3 P3,
    out float3 Normal)
{
    const float3 Center = Box == 0 ?
        float3(-0.38, 1.42, 0.34) : float3(0.38, 1.23, 0.58);
    const float3 HalfSize = Box == 0 ?
        float3(0.29, 0.36, 0.34) : float3(0.26, 0.31, 0.58);
    const float Angle = Box == 0 ? -0.28 : 0.34;

    float3 L0;
    float3 L1;
    float3 L2;
    float3 L3;
    float3 N;
    if (Face == 0)
    {
        L0 = float3(-1,-1,-1); L1 = float3(-1, 1,-1);
        L2 = float3(-1, 1, 1); L3 = float3(-1,-1, 1); N = float3(-1,0,0);
    }
    else if (Face == 1)
    {
        L0 = float3(1, 1,-1); L1 = float3(1,-1,-1);
        L2 = float3(1,-1, 1); L3 = float3(1, 1, 1); N = float3(1,0,0);
    }
    else if (Face == 2)
    {
        L0 = float3(1,-1,-1); L1 = float3(-1,-1,-1);
        L2 = float3(-1,-1, 1); L3 = float3(1,-1, 1); N = float3(0,-1,0);
    }
    else if (Face == 3)
    {
        L0 = float3(-1,1,-1); L1 = float3(1,1,-1);
        L2 = float3(1,1, 1); L3 = float3(-1,1, 1); N = float3(0,1,0);
    }
    else if (Face == 4)
    {
        L0 = float3(-1,-1,-1); L1 = float3(1,-1,-1);
        L2 = float3(1,1,-1); L3 = float3(-1,1,-1); N = float3(0,0,-1);
    }
    else
    {
        L0 = float3(-1,1,1); L1 = float3(1,1,1);
        L2 = float3(1,-1,1); L3 = float3(-1,-1,1); N = float3(0,0,1);
    }
    P0 = RotateZ(L0 * HalfSize, Angle) + Center;
    P1 = RotateZ(L1 * HalfSize, Angle) + Center;
    P2 = RotateZ(L2 * HalfSize, Angle) + Center;
    P3 = RotateZ(L3 * HalfSize, Angle) + Center;
    Normal = normalize(RotateZ(N, Angle));
}

float3 CubeFacePosition(uint Face, float U, float V)
{
    float3 FaceNormal;
    float3 FaceU;
    float3 FaceV;
    if (Face == 0)
    {
        FaceNormal = float3(1,0,0);
        FaceU = float3(0,1,0);
        FaceV = float3(0,0,1);
    }
    else if (Face == 1)
    {
        FaceNormal = float3(-1,0,0);
        FaceU = float3(0,-1,0);
        FaceV = float3(0,0,1);
    }
    else if (Face == 2)
    {
        FaceNormal = float3(0,1,0);
        FaceU = float3(-1,0,0);
        FaceV = float3(0,0,1);
    }
    else if (Face == 3)
    {
        FaceNormal = float3(0,-1,0);
        FaceU = float3(1,0,0);
        FaceV = float3(0,0,1);
    }
    else if (Face == 4)
    {
        FaceNormal = float3(0,0,1);
        FaceU = float3(1,0,0);
        FaceV = float3(0,1,0);
    }
    else
    {
        FaceNormal = float3(0,0,-1);
        FaceU = float3(-1,0,0);
        FaceV = float3(0,1,0);
    }
    return FaceNormal + U * FaceU + V * FaceV;
}

// A spherified-cube projection keeps the six cubemap grids continuous while
// reducing the face-center/corner area distortion of simple normalization.
float3 CubeToSphere(float3 Cube)
{
    const float3 Square = Cube * Cube;
    float3 Sphere;
    Sphere.x = Cube.x * sqrt(max(0.0,
        1.0 - 0.5 * Square.y - 0.5 * Square.z +
        Square.y * Square.z / 3.0));
    Sphere.y = Cube.y * sqrt(max(0.0,
        1.0 - 0.5 * Square.z - 0.5 * Square.x +
        Square.z * Square.x / 3.0));
    Sphere.z = Cube.z * sqrt(max(0.0,
        1.0 - 0.5 * Square.x - 0.5 * Square.y +
        Square.x * Square.y / 3.0));
    return normalize(Sphere);
}

float3 SphereDirection(uint Face, uint GridX, uint GridY)
{
    const float2 Grid = float2(GridX, GridY) /
        float(kSphereFaceResolution);
    const float2 FaceUv = Grid * 2.0 - 1.0;
    return CubeToSphere(CubeFacePosition(Face, FaceUv.x, FaceUv.y));
}

void GetSphereTriangle(
    uint Sphere,
    uint Triangle,
    out float3 P0,
    out float3 P1,
    out float3 P2,
    out float3 N0,
    out float3 N1,
    out float3 N2,
    out uint MaterialId)
{
    const float3 Center = Sphere == 0 ?
        float3(-0.48, 0.58, 0.30) : float3(0.49, 0.56, 0.25);
    const float Radius = Sphere == 0 ? 0.30 : 0.25;
    MaterialId = Sphere == 0 ? 4 : 5;

    const uint Face = Triangle / kSphereTrianglesPerFace;
    const uint FaceTriangle = Triangle % kSphereTrianglesPerFace;
    const uint Cell = FaceTriangle / 2;
    const uint Half = FaceTriangle & 1;
    const uint CellX = Cell % kSphereFaceResolution;
    const uint CellY = Cell / kSphereFaceResolution;
    const float3 A = SphereDirection(Face, CellX, CellY);
    const float3 B = SphereDirection(Face, CellX + 1, CellY);
    const float3 C = SphereDirection(Face, CellX + 1, CellY + 1);
    const float3 D = SphereDirection(Face, CellX, CellY + 1);

    // Alternate the quad diagonal to avoid a directional bias across each face.
    if (((CellX + CellY) & 1) == 0)
    {
        if (Half == 0)
        {
            N0 = A; N1 = B; N2 = C;
        }
        else
        {
            N0 = A; N1 = C; N2 = D;
        }
    }
    else if (Half == 0)
    {
        N0 = A; N1 = B; N2 = D;
    }
    else
    {
        N0 = B; N1 = C; N2 = D;
    }
    P0 = Center + N0 * Radius;
    P1 = Center + N1 * Radius;
    P2 = Center + N2 * Radius;
}

CornellVertex MakeVertex(float3 Position, float3 Normal, uint MaterialId)
{
    CornellVertex Vertex;
    Vertex.Position = Position;
    Vertex.Padding = 0.0;
    Vertex.Normal = normalize(Normal);
    Vertex.MaterialId = MaterialId;
    return Vertex;
}

CornellMaterial MakeMaterial(
    float3 BaseColor,
    float Roughness,
    float3 Emission,
    float Metallic,
    float Transmission,
    float Ior)
{
    CornellMaterial Material;
    Material.BaseColor = BaseColor;
    Material.Roughness = Roughness;
    Material.Emission = Emission;
    Material.Metallic = Metallic;
    Material.Transmission = Transmission;
    Material.Ior = Ior;
    Material.Padding = 0.0;
    return Material;
}

[numthreads(64, 1, 1)]
void GenerateCornellGeometryCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    const uint Triangle = DispatchThreadId.x;
    if (Triangle < 7)
    {
        if (Triangle == 0) Materials[0] = MakeMaterial(float3(0.78,0.78,0.72),0.72,0,0,0,1.5);
        if (Triangle == 1) Materials[1] = MakeMaterial(float3(0.72,0.055,0.035),0.78,0,0,0,1.5);
        if (Triangle == 2) Materials[2] = MakeMaterial(float3(0.055,0.56,0.095),0.76,0,0,0,1.5);
        if (Triangle == 3) Materials[3] = MakeMaterial(float3(0.68,0.64,0.55),0.48,0,0,0,1.5);
        if (Triangle == 4) Materials[4] = MakeMaterial(float3(0.92,0.64,0.22),0.16,0,0.94,0,1.5);
        if (Triangle == 5) Materials[5] = MakeMaterial(float3(0.96,0.985,1.0),0.02,0,0,1,1.5);
        if (Triangle == 6) Materials[6] = MakeMaterial(float3(1,1,1),0.0,float3(18,16,12),0,0,1.0);
    }
    if (Triangle >= kTriangleCount)
        return;

    float3 P0;
    float3 P1;
    float3 P2;
    float3 N0;
    float3 N1;
    float3 N2;
    uint MaterialId;
    const uint QuadTriangleCount = kRoomAndBoxQuadCount * 2;
    if (Triangle < QuadTriangleCount)
    {
        const uint Quad = Triangle / 2;
        const uint Half = Triangle & 1;
        float3 Q0;
        float3 Q1;
        float3 Q2;
        float3 Q3;
        float3 Normal;
        if (Quad < 6)
        {
            GetRoomQuad(Quad, Q0, Q1, Q2, Q3, Normal, MaterialId);
        }
        else
        {
            GetBoxQuad((Quad - 6) / 6, (Quad - 6) % 6,
                Q0, Q1, Q2, Q3, Normal);
            MaterialId = 3;
        }
        if (Half == 0)
        {
            P0 = Q0; P1 = Q1; P2 = Q2;
        }
        else
        {
            P0 = Q0; P1 = Q2; P2 = Q3;
        }
        N0 = Normal; N1 = Normal; N2 = Normal;
    }
    else
    {
        const uint SphereTriangle = Triangle - QuadTriangleCount;
        const uint Sphere = SphereTriangle / kSphereTriangleCount;
        GetSphereTriangle(Sphere, SphereTriangle % kSphereTriangleCount,
            P0, P1, P2, N0, N1, N2, MaterialId);
    }

    const uint VertexBase = Triangle * 3;
    Vertices[VertexBase + 0] = MakeVertex(P0, N0, MaterialId);
    Vertices[VertexBase + 1] = MakeVertex(P1, N1, MaterialId);
    Vertices[VertexBase + 2] = MakeVertex(P2, N2, MaterialId);
    Indices[VertexBase + 0] = VertexBase + 0;
    Indices[VertexBase + 1] = VertexBase + 1;
    Indices[VertexBase + 2] = VertexBase + 2;
}
