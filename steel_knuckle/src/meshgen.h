// =============================================================================
//  meshgen.h - the handful of primitives every body part and prop is built from.
// =============================================================================
#pragma once

#include <vector>
#include "mathx.h"

struct MeshVertex {
    float3 pos;
    float3 normal;
    float2 uv{0, 0};
    float4 color{1, 1, 1, 1};
    float4 bones{0, 0, 0, 0};
    float4 weights{0, 0, 0, 0};
};

struct MeshData {
    std::vector<MeshVertex> verts;
    std::vector<unsigned int> indices;
};

// Unit sphere, radius 1, centred on the origin.
MeshData GenSphere(int rings, int slices);

// Unit cylinder, radius 1, base at y = 0, top at y = 1. Capped at both ends so
// it reads solid when used as a bone segment.
MeshData GenCylinder(int slices);

// Unit cube, 1x1x1, centred on the origin.
MeshData GenCube();

// Unit cone, base radius 1 at y = 0, apex at y = 1.
MeshData GenCone(int slices);
