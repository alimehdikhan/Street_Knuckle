#include "meshgen.h"

// Winding is clockwise when viewed from outside, matching the front-face
// convention the pipelines are created with.
static void Tri(MeshData &m, unsigned int a, unsigned int b, unsigned int c) {
    m.indices.push_back(a);
    m.indices.push_back(b);
    m.indices.push_back(c);
}

MeshData GenSphere(int rings, int slices) {
    MeshData m;
    for (int r = 0; r <= rings; r++) {
        float phi = PI * (float)r / rings;              // 0 at +Y pole
        float y = cosf(phi), rad = sinf(phi);
        for (int s = 0; s <= slices; s++) {
            float th = 2 * PI * (float)s / slices;
            float3 n = { rad * cosf(th), y, rad * sinf(th) };
            m.verts.push_back({ n, n });
        }
    }
    int stride = slices + 1;
    for (int r = 0; r < rings; r++) {
        for (int s = 0; s < slices; s++) {
            unsigned int i0 = r * stride + s, i1 = i0 + 1;
            unsigned int i2 = i0 + stride, i3 = i2 + 1;
            Tri(m, i0, i2, i1);
            Tri(m, i1, i2, i3);
        }
    }
    return m;
}

MeshData GenCylinder(int slices) {
    MeshData m;
    // side wall: duplicated rings so the seam normals stay radial
    for (int s = 0; s <= slices; s++) {
        float th = 2 * PI * (float)s / slices;
        float3 n = { cosf(th), 0, sinf(th) };
        m.verts.push_back({ { n.x, 0, n.z }, n });
        m.verts.push_back({ { n.x, 1, n.z }, n });
    }
    for (int s = 0; s < slices; s++) {
        unsigned int i0 = s * 2, i1 = i0 + 1, i2 = i0 + 2, i3 = i0 + 3;
        Tri(m, i0, i2, i1);
        Tri(m, i1, i2, i3);
    }
    // caps, with their own flat normals
    unsigned int capBase = (unsigned int)m.verts.size();
    m.verts.push_back({ { 0, 0, 0 }, { 0, -1, 0 } });
    for (int s = 0; s <= slices; s++) {
        float th = 2 * PI * (float)s / slices;
        m.verts.push_back({ { cosf(th), 0, sinf(th) }, { 0, -1, 0 } });
    }
    for (int s = 0; s < slices; s++) Tri(m, capBase, capBase + 1 + s, capBase + 2 + s);

    unsigned int topBase = (unsigned int)m.verts.size();
    m.verts.push_back({ { 0, 1, 0 }, { 0, 1, 0 } });
    for (int s = 0; s <= slices; s++) {
        float th = 2 * PI * (float)s / slices;
        m.verts.push_back({ { cosf(th), 1, sinf(th) }, { 0, 1, 0 } });
    }
    for (int s = 0; s < slices; s++) Tri(m, topBase, topBase + 2 + s, topBase + 1 + s);
    return m;
}

MeshData GenCube() {
    MeshData m;
    static const float3 nrm[6] = {
        { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }
    };
    // per-face tangent basis, so each face gets crisp flat shading
    static const float3 uAxis[6] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { 1, 0, 0 }, { -1, 0, 0 }
    };
    static const float3 vAxis[6] = {
        { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, -1 }
    };
    for (int f = 0; f < 6; f++) {
        unsigned int base = (unsigned int)m.verts.size();
        for (int c = 0; c < 4; c++) {
            float su = (c == 0 || c == 3) ? -0.5f : 0.5f;
            float sv = (c < 2) ? -0.5f : 0.5f;
            float3 p = nrm[f] * 0.5f + uAxis[f] * su + vAxis[f] * sv;
            m.verts.push_back({ p, nrm[f] });
        }
        Tri(m, base, base + 1, base + 2);
        Tri(m, base, base + 2, base + 3);
    }
    return m;
}

MeshData GenCone(int slices) {
    MeshData m;
    for (int s = 0; s <= slices; s++) {
        float th = 2 * PI * (float)s / slices;
        float3 dir = { cosf(th), 0, sinf(th) };
        // slope normal for a cone of height 1 and base radius 1
        float3 n = VNorm(float3{ dir.x, 1.0f, dir.z });
        m.verts.push_back({ { dir.x, 0, dir.z }, n });
        m.verts.push_back({ { 0, 1, 0 }, n });
    }
    for (int s = 0; s < slices; s++) {
        unsigned int i0 = s * 2, i1 = i0 + 1, i2 = i0 + 2;
        Tri(m, i0, i2, i1);
    }
    unsigned int capBase = (unsigned int)m.verts.size();
    m.verts.push_back({ { 0, 0, 0 }, { 0, -1, 0 } });
    for (int s = 0; s <= slices; s++) {
        float th = 2 * PI * (float)s / slices;
        m.verts.push_back({ { cosf(th), 0, sinf(th) }, { 0, -1, 0 } });
    }
    for (int s = 0; s < slices; s++) Tri(m, capBase, capBase + 1 + s, capBase + 2 + s);
    return m;
}
