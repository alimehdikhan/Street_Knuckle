// =============================================================================
//  render.h - the drawing layer, on Diligent Engine.
//
//  Everything the game draws is a handful of primitives with a model matrix, a
//  colour and a surface response. Calls are queued per primitive and issued as
//  instanced draws, so a frame with several hundred body parts and props costs
//  a few draw calls rather than a few hundred.
//
//  Frame shape:
//      BeginFrame()            scene target bound and cleared
//        SkyRect / SkyCircle   backdrop, before any depth
//        BeginScene(...)       camera and lights uploaded
//          Mesh / Shadow / Trans / Line / Circle
//        EndScene()            queues flushed
//      Post(...)               bloom chain, tonemap, composite to backbuffer
//        UIRect / UIText ...   HUD, drawn straight onto the composite
//      EndFrame()              present
// =============================================================================
#pragma once

#include <vector>

#include "mathx.h"
#include "font.h"

enum MeshId {
    MESH_SPHERE = 0,
    MESH_CYL,
    MESH_CUBE,
    MESH_CONE,
    MESH_DISC,
    MESH_ANIME,
    MESH_RITUAL,
    MESH_COUNT
};

// Specular strength, highlight tightness, self-illumination, rim weight.
struct Surface {
    float spec = 0.20f;
    float gloss = 24.0f;
    float emis = 0.0f;
    float rim = 0.30f;
};

inline Surface SurfSkin()    { return { 0.28f, 30.0f, 0.00f, 0.34f }; }
inline Surface SurfCloth()   { return { 0.05f, 10.0f, 0.00f, 0.16f }; }
inline Surface SurfLeather() { return { 0.55f, 54.0f, 0.00f, 0.26f }; }
inline Surface SurfStone()   { return { -0.07f, 12.0f, 0.00f, 0.08f }; }
inline Surface SurfMetal()   { return { 0.85f, 80.0f, 0.00f, 0.40f }; }
inline Surface SurfPolish()  { return { -0.30f, 80.0f, 0.00f, 0.06f }; }
inline Surface SurfGlow(float e) { return { 0.0f, 8.0f, e, 0.0f }; }

struct SceneLighting {
    Vector3 viewPos{ 0, 0, 0 };
    Vector3 focus{ 0, 1, 0 };
    Vector3 lightDir{ -0.45f, -0.80f, -0.35f };
    Vector3 keyCol{ 2.3f, 2.55f, 3.0f };
    Vector3 skyCol{ 0.20f, 0.28f, 0.38f };
    Vector3 gndCol{ 0.06f, 0.065f, 0.075f };
    Vector3 rimCol{ 0.40f, 0.64f, 0.9f };
    Vector3 fogCol{ 0.027f, 0.045f, 0.066f };
    float fogAmt = 0.026f;
    Vector3 ptPos[4]{};
    Vector3 ptCol[4]{};
};

struct PostParams {
    float bloom = 0.22f;
    float vignette = 0.30f;
    float aberration = 0.0015f;
    float grain = 0.006f;
    Vector3 flashCol{ 1.0f, 0.85f, 0.6f };
    float flashAmt = 0.0f;
    float time = 0.0f;
};

struct CharacterAsset;
namespace R {

// Imported characters share the scene lighting and shadow passes.
bool LoadCharacterMesh(MeshId id, const CharacterAsset &asset);
void SetCharacterBones(int fighter, const Matrix *bones, int count);

// nativeWindow is an HWND. Returns false if no backend could be created.
bool Init(void *nativeWindow, int width, int height, bool preferVulkan);
void Shutdown();
void Resize(int width, int height);
const char *BackendName();
void SetQuality(int level); // 0 performance, 1 balanced, 2 cinematic
int Quality();
const char *QualityName();

int Width();
int Height();

// Size of the supersampled scene target, which is what the sky backdrop is
// drawn against.
void SceneSize(int &width, int &height);

// Projection matrix for the active backend, which decides the clip-space depth
// range (OpenGL's -1..1 versus Vulkan's 0..1).
Matrix Projection(float fovYRadians, float aspect, float zNear, float zFar);

void BeginFrame();
void EndFrame();

// --- backdrop, drawn into the scene target before any 3D ---
void SkyRectGradient(float x, float y, float w, float h, Color top, Color bottom);
void SkyDot(float x, float y, float r, Color c);

// --- 3D ---
void BeginScene(const Matrix &view, const Matrix &proj, const SceneLighting &lighting);
void Mesh(MeshId m, const Matrix &model, Color c, Surface s);         // opaque, depth write
void Ground(MeshId m, const Matrix &model, Color c);                  // contact patches, under the shadows
void Shadow(MeshId m, const Matrix &model, Color c);                  // flattened, unlit, depth write
void Trans(MeshId m, const Matrix &model, Color c, Surface s);        // blended, no depth write
void TransFlat(MeshId m, const Matrix &model, Color c);               // blended, unlit
void Line(Vector3 a, Vector3 b, Color c);
void CircleY(Vector3 centre, float radius, Color c);                  // ring lying flat on the floor
void CircleFacing(Vector3 centre, float radius, Vector3 normal, Color c);
void EndScene();

// --- post + overlay ---
void Post(const PostParams &p);
void UIRect(float x, float y, float w, float h, Color c);
void UIRectGradientH(float x, float y, float w, float h, Color left, Color right);
void UIRectGradientV(float x, float y, float w, float h, Color top, Color bottom);
void UISlant(float x, float y, float w, float h, float skew, Color left, Color right);
void UITextStyled(const char *s, float x, float y, float height, Color c,
                  float widthScale=1.0f, float tracking=0.0f, float italic=0.0f);
void UIRectLines(float x, float y, float w, float h, float thick, Color c);
void UIDiamond(float x,float y,float radius,Color c);
void UICircle(float cx, float cy, float r, Color c);
void UICircleLines(float cx, float cy, float r, float thick, Color c);
void UIText(const char *s, float x, float y, int scale, Color c);
void UITextShadowed(const char *s, float x, float y, int scale, Color c);
void UITextCentred(const char *s, float cx, float y, int scale, Color c);

// Grabs the finished frame just before it is presented. Ask before drawing the
// frame, then collect afterwards. Used by --shot for headless verification.
void RequestCapture();
bool TakeCapture(std::vector<unsigned char> &rgba, int &width, int &height);

}  // namespace R
