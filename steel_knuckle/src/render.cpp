// =============================================================================
//  render.cpp - Diligent Engine backend.
// =============================================================================
#include "render.h"
#include "meshgen.h"
#include "character.h"
#include "shaders.h"

#include <vector>
#include <cstdio>
#include <cstring>

#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "MapHelper.hpp"
#include "EngineFactoryOpenGL.h"
#include "EngineFactoryVk.h"
#include "NativeWindow.h"

using namespace Diligent;

namespace {

// ---- GPU-side vertex formats -------------------------------------------------
struct Instance {
    float4 row0, row1, row2, row3;
    float4 color;
    float4 surf;
};
struct LineVertex {
    float3 pos;
    float4 col;
};
struct UIVertex {
    float2 pos;
    float2 uv;
    float4 col;
};

// ---- constant buffer layouts -------------------------------------------------
struct FrameCB {
    float4 vp0, vp1, vp2, vp3;      // view-projection, one row per float4
    float4 viewPos, lightDir, keyCol, skyCol, gndCol, rimCol, fogCol;
    float4 ptPos[4], ptCol[4];
    float4 lightVP0, lightVP1, lightVP2, lightVP3, shadowInfo;
};
struct UICB { float4 screen; };
struct BlurCB { float4 dir; };
struct PostCB { float4 post, flash, misc; };

struct GpuMesh {
    RefCntAutoPtr<IBuffer> vb, ib;
    Uint32 indexCount = 0;
    RefCntAutoPtr<ITexture> albedo, normal, material;
    RefCntAutoPtr<IShaderResourceBinding> srb;
};

// Instance queues, one bucket per primitive. Draw order between buckets is what
// gives the scene its layering; within a bucket order does not matter because
// everything in it shares one depth/blend state.
struct Queue {
    std::vector<Instance> inst[MESH_COUNT];
    void clear() { for (auto &v : inst) v.clear(); }
    size_t total() const {
        size_t n = 0;
        for (auto &v : inst) n += v.size();
        return n;
    }
};

const int SHADOW_SIZE = 2048;
//                   // scene supersampling factor
const Uint32 MAX_INSTANCES = 16384;
const Uint32 MAX_LINE_VERTS = 32768;
const Uint32 MAX_UI_VERTS = 65536;

struct RState {
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> ctx;
    RefCntAutoPtr<ISwapChain> swap;
    RENDER_DEVICE_TYPE deviceType = RENDER_DEVICE_TYPE_UNDEFINED;
    const char *backend = "none";

    int width = 0, height = 0;
    int quality = 1;

    GpuMesh meshes[MESH_COUNT];

    // pipelines
    RefCntAutoPtr<IPipelineState> psoDepth;
    RefCntAutoPtr<IShaderResourceBinding> srbDepth;
    RefCntAutoPtr<ITexture> shadowTex, neutralNormal, neutralMaterial;
    ITextureView *shadowDSV = nullptr, *shadowSRV = nullptr;
    RefCntAutoPtr<IPipelineState> psoOpaque, psoShadow, psoGround, psoTrans, psoTransFlat;
    // The 2D batch draws to two different targets - the HDR scene buffer for the
    // sky and the swapchain for the HUD - so it needs a pipeline per format.
    RefCntAutoPtr<IPipelineState> psoLine, psoUISky, psoUIHud, psoBright, psoBlur, psoComposite;
    RefCntAutoPtr<IShaderResourceBinding> srbOpaque, srbShadow, srbGround, srbTrans, srbTransFlat;
    RefCntAutoPtr<IShaderResourceBinding> srbLine, srbUISky, srbUIHud;
    RefCntAutoPtr<IShaderResourceBinding> srbBright, srbBlurH, srbBlurV, srbComposite;

    // buffers
    RefCntAutoPtr<IBuffer> cbFrame, cbUI, cbBlur, cbPost, cbSkin;
    float4 skinRows[128]{};
    RefCntAutoPtr<ITexture> whiteTex;
    RefCntAutoPtr<IBuffer> instBuf, lineBuf, uiBuf, fsQuad;

    // offscreen targets
    RefCntAutoPtr<ITexture> sceneTex, sceneDepth, bloomTexA, bloomTexB;
    ITextureView *sceneRTV = nullptr, *sceneDSV = nullptr, *sceneSRV = nullptr;
    ITextureView *bloomRTVA = nullptr, *bloomSRVA = nullptr;
    ITextureView *bloomRTVB = nullptr, *bloomSRVB = nullptr;
    int sceneW = 0, sceneH = 0, bloomW = 0, bloomH = 0;

    RefCntAutoPtr<ITexture> atlasTex;
    ITextureView *atlasSRV = nullptr;

    // per-frame queues
    Queue qOpaque, qShadow, qGround, qTrans, qTransFlat;
    std::vector<LineVertex> lines;
    std::vector<UIVertex> sky, ui;

    bool captureRequested = false;
    RefCntAutoPtr<ITexture> captureTarget;
    std::vector<unsigned char> capture;
    int captureW = 0, captureH = 0;

    FrameCB frame{};
};

RState S;

const TEXTURE_FORMAT SCENE_FMT = TEX_FORMAT_RGBA16_FLOAT;
const TEXTURE_FORMAT DEPTH_FMT = TEX_FORMAT_D32_FLOAT;

// -----------------------------------------------------------------------------
// shader + pipeline helpers
// -----------------------------------------------------------------------------
RefCntAutoPtr<ITexture> MakeMaterialTexture(const unsigned char *pixels, int w, int h, bool srgb, const char *name) {
    std::vector<std::vector<unsigned char>> levels;
    std::vector<TextureSubResData> subs;
    levels.reserve(16); subs.reserve(16);
    TextureSubResData top; top.pData=pixels; top.Stride=w*4; subs.push_back(top);
    const unsigned char *previous=pixels;
    int pw=w,ph=h;
    while(pw>1 || ph>1) {
        int nw=std::max(1,pw/2),nh=std::max(1,ph/2);
        levels.emplace_back(size_t(nw)*nh*4);
        auto &next=levels.back();
        for(int y=0;y<nh;++y) for(int x=0;x<nw;++x) for(int c=0;c<4;++c) {
            float sum=0;
            for(int oy=0;oy<2;++oy) for(int ox=0;ox<2;++ox) {
                float v=previous[(size_t(std::min(y*2+oy,ph-1))*pw+std::min(x*2+ox,pw-1))*4+c]/255.0f;
                sum+=srgb && c<3 ? v*v : v;
            }
            float v=sum*0.25f;
            next[(size_t(y)*nw+x)*4+c]=(unsigned char)(255*(srgb && c<3 ? sqrtf(v) : v)+0.5f);
        }
        TextureSubResData sub; sub.pData=next.data(); sub.Stride=nw*4; subs.push_back(sub);
        previous=next.data();pw=nw;ph=nh;
    }
    TextureDesc td; td.Name=name; td.Type=RESOURCE_DIM_TEX_2D;
    td.Width=w;td.Height=h;td.MipLevels=(Uint32)subs.size();
    td.Format=srgb?TEX_FORMAT_RGBA8_UNORM_SRGB:TEX_FORMAT_RGBA8_UNORM;
    td.BindFlags=BIND_SHADER_RESOURCE;td.Usage=USAGE_IMMUTABLE;
    TextureData data;data.pSubResources=subs.data();data.NumSubresources=(Uint32)subs.size();
    RefCntAutoPtr<ITexture> texture; S.device->CreateTexture(td,&data,&texture);
    return texture;
}

RefCntAutoPtr<IShader> MakeShader(SHADER_TYPE type, const char *name, const std::string &src) {
    ShaderCreateInfo ci;
    ci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    ci.Desc.ShaderType = type;
    ci.Desc.Name = name;
    ci.Desc.UseCombinedTextureSamplers = true;
    ci.EntryPoint = "main";
    ci.Source = src.c_str();
    ci.SourceLength = src.size();
    RefCntAutoPtr<IShader> sh;
    S.device->CreateShader(ci, &sh);
    if (!sh) std::printf("[render] shader '%s' failed to compile\n", name);
    return sh;
}

void SetBlendAlpha(BlendStateDesc &bs) {
    bs.RenderTargets[0].BlendEnable = True;
    bs.RenderTargets[0].SrcBlend = BLEND_FACTOR_SRC_ALPHA;
    bs.RenderTargets[0].DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
    bs.RenderTargets[0].BlendOp = BLEND_OPERATION_ADD;
    bs.RenderTargets[0].SrcBlendAlpha = BLEND_FACTOR_ONE;
    bs.RenderTargets[0].DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
    bs.RenderTargets[0].BlendOpAlpha = BLEND_OPERATION_ADD;
}

// Input layout shared by every instanced mesh pipeline: geometry in slot 0,
// per-instance transform and material in slot 1.
// Per-instance elements have to spell out RelativeOffset and Stride, because
// those parameters sit between IsNormalized and Frequency.
LayoutElement InstanceElem(Uint32 index) {
    return LayoutElement{ index, 1, 4, VT_FLOAT32, False,
                          LAYOUT_ELEMENT_AUTO_OFFSET, LAYOUT_ELEMENT_AUTO_STRIDE,
                          INPUT_ELEMENT_FREQUENCY_PER_INSTANCE };
}

void MeshLayout(std::vector<LayoutElement> &e) {
    e.clear();
    e.push_back(LayoutElement{ 0, 0, 3, VT_FLOAT32, False });      // position
    e.push_back(LayoutElement{ 1, 0, 3, VT_FLOAT32, False });      // normal
    for (Uint32 i = 0; i < 4; i++) e.push_back(InstanceElem(2 + i));   // model matrix rows
    e.push_back(InstanceElem(6));                                  // colour
    e.push_back(LayoutElement{ 8, 0, 2, VT_FLOAT32, False });
    e.push_back(LayoutElement{ 9, 0, 4, VT_FLOAT32, False });
    e.push_back(LayoutElement{ 10, 0, 4, VT_FLOAT32, False });
    e.push_back(LayoutElement{ 11, 0, 4, VT_FLOAT32, False });
    e.push_back(InstanceElem(7));                                  // surface
}

RefCntAutoPtr<IPipelineState> MakeMeshPSO(const char *name, bool lit, bool depthWrite, bool blend,
                                          CULL_MODE cull, TEXTURE_FORMAT rtv, TEXTURE_FORMAT dsv, bool depthOnly = false) {
    std::vector<LayoutElement> elems;
    MeshLayout(elems);

    std::string vsSrc = std::string(depthOnly ? "#define SHADOW_PASS 1\n" : "") + HLSL_COMMON + HLSL_MESH_VS;
    std::string psSrc = std::string(HLSL_COMMON) + (lit ? HLSL_MESH_PS : HLSL_FLAT_PS);
    auto vs = MakeShader(SHADER_TYPE_VERTEX, "mesh.vs", vsSrc);
    RefCntAutoPtr<IShader> ps;
    if (!depthOnly) ps = MakeShader(SHADER_TYPE_PIXEL, lit ? "mesh.ps" : "flat.ps", psSrc);

    GraphicsPipelineStateCreateInfo pci;
    pci.PSODesc.Name = name;
    pci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
    pci.GraphicsPipeline.NumRenderTargets = depthOnly ? 0 : 1;
    pci.GraphicsPipeline.RTVFormats[0] = rtv;
    pci.GraphicsPipeline.DSVFormat = dsv;
    pci.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pci.GraphicsPipeline.RasterizerDesc.CullMode = cull;
    pci.GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = False;
    pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = True;
    pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = depthWrite ? True : False;
    pci.GraphicsPipeline.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS;
    if (blend) SetBlendAlpha(pci.GraphicsPipeline.BlendDesc);
    pci.GraphicsPipeline.InputLayout.LayoutElements = elems.data();
    pci.GraphicsPipeline.InputLayout.NumElements = (Uint32)elems.size();
    pci.pVS = vs;
    pci.pPS = ps;
    pci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

    ShaderResourceVariableDesc vars[] = {
        {SHADER_TYPE_PIXEL,"g_Albedo",SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {SHADER_TYPE_PIXEL,"g_Normal",SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {SHADER_TYPE_PIXEL,"g_Material",SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };
    SamplerDesc sampler;
    sampler.MinFilter = sampler.MagFilter = sampler.MipFilter = FILTER_TYPE_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_WRAP;
    SamplerDesc shadowSampler;
    shadowSampler.MinFilter = shadowSampler.MagFilter = shadowSampler.MipFilter = FILTER_TYPE_POINT;
    shadowSampler.AddressU = shadowSampler.AddressV = shadowSampler.AddressW = TEXTURE_ADDRESS_CLAMP;
    ImmutableSamplerDesc samplers[] = {
        {SHADER_TYPE_PIXEL,"g_Albedo",sampler}, {SHADER_TYPE_PIXEL,"g_Normal",sampler},
        {SHADER_TYPE_PIXEL,"g_Material",sampler}, {SHADER_TYPE_PIXEL,"g_ShadowMap",shadowSampler}
    };
    if (lit) {
        pci.PSODesc.ResourceLayout.Variables = vars;
        pci.PSODesc.ResourceLayout.NumVariables = 3;
        pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
        pci.PSODesc.ResourceLayout.NumImmutableSamplers = 4;
    }
    RefCntAutoPtr<IPipelineState> pso;
    S.device->CreateGraphicsPipelineState(pci, &pso);
    if (pso) {
        if (auto *v = pso->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_ShadowMap")) v->Set(S.shadowSRV);
        if (auto *v = pso->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Skin")) v->Set(S.cbSkin);
        if (auto *v = pso->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Frame")) v->Set(S.cbFrame);
        if (auto *v = pso->GetStaticVariableByName(SHADER_TYPE_PIXEL, "Frame")) v->Set(S.cbFrame);
    } else {
        std::printf("[render] pipeline '%s' failed\n", name);
    }
    return pso;
}

// -----------------------------------------------------------------------------
// offscreen targets
// -----------------------------------------------------------------------------
void CreateTarget(RefCntAutoPtr<ITexture> &tex, ITextureView **rtv, ITextureView **srv,
                  int w, int h, TEXTURE_FORMAT fmt, const char *name) {
    tex.Release();
    TextureDesc td;
    td.Name = name;
    td.Type = RESOURCE_DIM_TEX_2D;
    td.Width = (Uint32)w;
    td.Height = (Uint32)h;
    td.MipLevels = 1;
    td.Format = fmt;
    td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    td.Usage = USAGE_DEFAULT;
    S.device->CreateTexture(td, nullptr, &tex);
    if (rtv) *rtv = tex->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    if (srv) *srv = tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}

void CreateDepth(RefCntAutoPtr<ITexture> &tex, ITextureView **dsv, int w, int h, const char *name) {
    tex.Release();
    TextureDesc td;
    td.Name = name;
    td.Type = RESOURCE_DIM_TEX_2D;
    td.Width = (Uint32)w;
    td.Height = (Uint32)h;
    td.MipLevels = 1;
    td.Format = DEPTH_FMT;
    td.BindFlags = BIND_DEPTH_STENCIL;
    td.Usage = USAGE_DEFAULT;
    S.device->CreateTexture(td, nullptr, &tex);
    if (dsv) *dsv = tex->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
}

void BindPostResources();

void CreateTargets(int w, int h) {
    float scale = S.quality == 2 ? 1.5f : S.quality == 0 ? 0.8f : 1.0f;
    S.sceneW = std::max(1, int(w * scale));
    S.sceneH = std::max(1, int(h * scale));
    S.bloomW = w / 2 > 1 ? w / 2 : 1;
    S.bloomH = h / 2 > 1 ? h / 2 : 1;
    CreateTarget(S.sceneTex, &S.sceneRTV, &S.sceneSRV, S.sceneW, S.sceneH, SCENE_FMT, "scene");
    CreateDepth(S.sceneDepth, &S.sceneDSV, S.sceneW, S.sceneH, "scene depth");
    CreateTarget(S.bloomTexA, &S.bloomRTVA, &S.bloomSRVA, S.bloomW, S.bloomH, SCENE_FMT, "bloomA");
    CreateTarget(S.bloomTexB, &S.bloomRTVB, &S.bloomSRVB, S.bloomW, S.bloomH, SCENE_FMT, "bloomB");
    BindPostResources();
}

void BindPostResources() {
    auto bind = [](RefCntAutoPtr<IShaderResourceBinding> &srb, const char *name, ITextureView *view) {
        if (!srb) return;
        if (auto *v = srb->GetVariableByName(SHADER_TYPE_PIXEL, name)) v->Set(view);
    };
    bind(S.srbBright, "g_Scene", S.sceneSRV);
    bind(S.srbBlurH, "g_Scene", S.bloomSRVA);
    bind(S.srbBlurV, "g_Scene", S.bloomSRVB);
    bind(S.srbComposite, "g_Scene", S.sceneSRV);
    bind(S.srbComposite, "g_Bloom", S.bloomSRVA);
}

// -----------------------------------------------------------------------------
// queue helpers
// -----------------------------------------------------------------------------
Instance MakeInstance(const Matrix &m, Color c, Surface s) {
    Instance in;
    in.row0 = { m._11, m._12, m._13, m._14 };
    in.row1 = { m._21, m._22, m._23, m._24 };
    in.row2 = { m._31, m._32, m._33, m._34 };
    in.row3 = { m._41, m._42, m._43, m._44 };
    in.color = ToF4(c);
    in.surf = { s.spec, s.gloss, s.emis, s.rim };
    return in;
}

// Copies every queue into the shared instance buffer, recording where each
// bucket starts so a draw can point the vertex stream straight at it.
struct Batch {
    MeshId mesh;
    Uint32 offsetBytes;
    Uint32 count;
};

std::vector<Batch> BuildBatches(std::vector<Instance> &dst, const Queue &q) {
    std::vector<Batch> out;
    for (int m = 0; m < MESH_COUNT; m++) {
        const auto &v = q.inst[m];
        if (v.empty()) continue;
        Batch b;
        b.mesh = (MeshId)m;
        b.offsetBytes = (Uint32)(dst.size() * sizeof(Instance));
        b.count = (Uint32)v.size();
        out.push_back(b);
        dst.insert(dst.end(), v.begin(), v.end());
    }
    return out;
}

void DrawBatches(IPipelineState *pso, IShaderResourceBinding *srb, const std::vector<Batch> &batches) {
    if (batches.empty() || !pso) return;
    S.ctx->SetPipelineState(pso);
    if (srb) S.ctx->CommitShaderResources(srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    for (const auto &b : batches) {
        const GpuMesh &gm = S.meshes[b.mesh];
        if (!gm.vb || !gm.ib) continue;
        auto *binding = pso == S.psoOpaque.RawPtr() && gm.srb ? gm.srb.RawPtr() : srb;
        if (binding) S.ctx->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        IBuffer *vbs[2] = { gm.vb, S.instBuf };
        Uint64 offsets[2] = { 0, b.offsetBytes };
        S.ctx->SetVertexBuffers(0, 2, vbs, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                SET_VERTEX_BUFFERS_FLAG_RESET);
        S.ctx->SetIndexBuffer(gm.ib, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        DrawIndexedAttribs da;
        da.IndexType = VT_UINT32;
        da.NumIndices = gm.indexCount;
        da.NumInstances = b.count;
        da.Flags = DRAW_FLAG_VERIFY_ALL;
        S.ctx->DrawIndexed(da);
    }
}

void PushUIQuad(std::vector<UIVertex> &dst, float x, float y, float w, float h,
                GlyphUV uv, Color c0, Color c1) {
    float4 a = ToF4(c0), b = ToF4(c1);
    UIVertex v0{ { x, y },         { uv.u0, uv.v0 }, a };
    UIVertex v1{ { x + w, y },     { uv.u1, uv.v0 }, b };
    UIVertex v2{ { x + w, y + h }, { uv.u1, uv.v1 }, b };
    UIVertex v3{ { x, y + h },     { uv.u0, uv.v1 }, a };
    dst.push_back(v0); dst.push_back(v1); dst.push_back(v2);
    dst.push_back(v0); dst.push_back(v2); dst.push_back(v3);
}

void PushUITri(std::vector<UIVertex> &dst, float2 a, float2 b, float2 c, Color col) {
    GlyphUV w = FontWhite();
    float4 f = ToF4(col);
    dst.push_back({ a, { w.u0, w.v0 }, f });
    dst.push_back({ b, { w.u0, w.v0 }, f });
    dst.push_back({ c, { w.u0, w.v0 }, f });
}

void FlushUIBatch(std::vector<UIVertex> &verts, int screenW, int screenH,
                  IPipelineState *pso, IShaderResourceBinding *srb) {
    if (verts.empty() || !pso) return;
    Uint32 n = (Uint32)verts.size();
    if (n > MAX_UI_VERTS) n = MAX_UI_VERTS;
    {
        MapHelper<UIVertex> m(S.ctx, S.uiBuf, MAP_WRITE, MAP_FLAG_DISCARD);
        memcpy(m, verts.data(), n * sizeof(UIVertex));
    }
    {
        MapHelper<UICB> m(S.ctx, S.cbUI, MAP_WRITE, MAP_FLAG_DISCARD);
        m->screen = { (float)screenW, (float)screenH, 0, 0 };
    }
    S.ctx->SetPipelineState(pso);
    S.ctx->CommitShaderResources(srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    IBuffer *vbs[1] = { S.uiBuf };
    Uint64 off[1] = { 0 };
    S.ctx->SetVertexBuffers(0, 1, vbs, off, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                            SET_VERTEX_BUFFERS_FLAG_RESET);
    DrawAttribs da;
    da.NumVertices = n;
    da.Flags = DRAW_FLAG_VERIFY_ALL;
    S.ctx->Draw(da);
    verts.clear();
}

void FullscreenPass(IPipelineState *pso, IShaderResourceBinding *srb) {
    if (!pso) return;
    S.ctx->SetPipelineState(pso);
    if (srb) S.ctx->CommitShaderResources(srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    IBuffer *vbs[1] = { S.fsQuad };
    Uint64 off[1] = { 0 };
    S.ctx->SetVertexBuffers(0, 1, vbs, off, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                            SET_VERTEX_BUFFERS_FLAG_RESET);
    DrawAttribs da;
    da.NumVertices = 6;
    da.Flags = DRAW_FLAG_VERIFY_ALL;
    S.ctx->Draw(da);
}

void SetTarget(ITextureView *rtv, ITextureView *dsv, int w, int h) {
    ITextureView *rtvs[1] = { rtv };
    S.ctx->SetRenderTargets(rtv ? 1 : 0, rtv ? rtvs : nullptr, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Viewport vp;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = (float)w;
    vp.Height = (float)h;
    vp.MinDepth = 0;
    vp.MaxDepth = 1;
    S.ctx->SetViewports(1, &vp, (Uint32)w, (Uint32)h);
}

void UploadMesh(MeshId id, const MeshData &md) {
    GpuMesh &gm = S.meshes[id];
    BufferDesc bd;
    bd.Name = "mesh vb";
    bd.Usage = USAGE_IMMUTABLE;
    bd.BindFlags = BIND_VERTEX_BUFFER;
    bd.Size = md.verts.size() * sizeof(MeshVertex);
    BufferData data{ md.verts.data(), bd.Size };
    S.device->CreateBuffer(bd, &data, &gm.vb);

    BufferDesc ibd;
    ibd.Name = "mesh ib";
    ibd.Usage = USAGE_IMMUTABLE;
    ibd.BindFlags = BIND_INDEX_BUFFER;
    ibd.Size = md.indices.size() * sizeof(unsigned int);
    BufferData idata{ md.indices.data(), ibd.Size };
    S.device->CreateBuffer(ibd, &idata, &gm.ib);
    gm.indexCount = (Uint32)md.indices.size();
}

RefCntAutoPtr<IBuffer> MakeCB(const char *name, Uint64 size) {
    BufferDesc bd;
    bd.Name = name;
    bd.Size = size;
    bd.Usage = USAGE_DYNAMIC;
    bd.BindFlags = BIND_UNIFORM_BUFFER;
    bd.CPUAccessFlags = CPU_ACCESS_WRITE;
    RefCntAutoPtr<IBuffer> b;
    S.device->CreateBuffer(bd, nullptr, &b);
    return b;
}

RefCntAutoPtr<IBuffer> MakeDynamicVB(const char *name, Uint64 size) {
    BufferDesc bd;
    bd.Name = name;
    bd.Size = size;
    bd.Usage = USAGE_DYNAMIC;
    bd.BindFlags = BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = CPU_ACCESS_WRITE;
    RefCntAutoPtr<IBuffer> b;
    S.device->CreateBuffer(bd, nullptr, &b);
    return b;
}

}  // namespace

// =============================================================================
// public interface
// =============================================================================
namespace R {

const char *BackendName() { return S.backend; }
int Quality() { return S.quality; }
const char *QualityName() { static const char *names[]={"PERFORMANCE","BALANCED","CINEMATIC"}; return names[S.quality]; }
void SetQuality(int level) {
    level=std::max(0,std::min(2,level));
    if(level==S.quality)return;
    S.quality=level; CreateTargets(S.width,S.height);
}
int Width() { return S.width; }
int Height() { return S.height; }
void SceneSize(int &width, int &height) { width = S.sceneW; height = S.sceneH; }

Matrix Projection(float fovY, float aspect, float zn, float zf) {
    const bool isGL = S.deviceType == RENDER_DEVICE_TYPE_GL ||
                      S.deviceType == RENDER_DEVICE_TYPE_GLES;
    return float4x4::Projection(fovY, aspect, zn, zf, isGL);
}

bool Init(void *nativeWindow, int width, int height, bool preferVulkan) {
    S.width = width;
    S.height = height;

    SwapChainDesc scd;
    scd.ColorBufferFormat = TEX_FORMAT_RGBA8_UNORM;
    scd.DepthBufferFormat = TEX_FORMAT_D32_FLOAT;

    if (preferVulkan) {
        if (auto *fac = GetEngineFactoryVk()) {
            EngineVkCreateInfo ci;
            fac->CreateDeviceAndContextsVk(ci, &S.device, &S.ctx);
            if (S.device) {
                Win32NativeWindow wnd{ nativeWindow };
                fac->CreateSwapChainVk(S.device, S.ctx, scd, wnd, &S.swap);
                if (S.swap) {
                    S.deviceType = RENDER_DEVICE_TYPE_VULKAN;
                    S.backend = "Vulkan";
                }
            }
        }
    }
    if (!S.swap) {
        S.device.Release();
        S.ctx.Release();
        if (auto *fac = GetEngineFactoryOpenGL()) {
            EngineGLCreateInfo ci;
            ci.Window.hWnd = nativeWindow;
            fac->CreateDeviceAndSwapChainGL(ci, &S.device, &S.ctx, scd, &S.swap);
            if (S.swap) {
                S.deviceType = RENDER_DEVICE_TYPE_GL;
                S.backend = "OpenGL";
            }
        }
    }
    if (!S.device || !S.ctx || !S.swap) {
        std::printf("[render] no graphics backend could be created\n");
        return false;
    }
    std::printf("[render] backend: %s\n", S.backend);

    // ---- geometry ----
    UploadMesh(MESH_SPHERE, GenSphere(14, 20));
    UploadMesh(MESH_CYL, GenCylinder(16));
    UploadMesh(MESH_CUBE, GenCube());
    UploadMesh(MESH_CONE, GenCone(7));
    UploadMesh(MESH_DISC, GenCylinder(64));

    // ---- constant + dynamic buffers ----
    S.cbFrame = MakeCB("frame cb", sizeof(FrameCB));
    S.cbSkin = MakeCB("skin cb", sizeof(S.skinRows));
    S.cbUI = MakeCB("ui cb", sizeof(UICB));
    S.cbBlur = MakeCB("blur cb", sizeof(BlurCB));
    S.cbPost = MakeCB("post cb", sizeof(PostCB));
    S.instBuf = MakeDynamicVB("instances", (Uint64)MAX_INSTANCES * sizeof(Instance));
    S.lineBuf = MakeDynamicVB("lines", (Uint64)MAX_LINE_VERTS * sizeof(LineVertex));
    S.uiBuf = MakeDynamicVB("ui", (Uint64)MAX_UI_VERTS * sizeof(UIVertex));
    {
        // clip-space quad with top-left origin UVs
        const float quad[6][4] = {
            { -1, 1, 0, 0 }, { 1, 1, 1, 0 }, { 1, -1, 1, 1 },
            { -1, 1, 0, 0 }, { 1, -1, 1, 1 }, { -1, -1, 0, 1 },
        };
        BufferDesc bd;
        bd.Name = "fullscreen quad";
        bd.Usage = USAGE_IMMUTABLE;
        bd.BindFlags = BIND_VERTEX_BUFFER;
        bd.Size = sizeof(quad);
        BufferData data{ quad, sizeof(quad) };
        S.device->CreateBuffer(bd, &data, &S.fsQuad);
    }

    // ---- font atlas ----
    int aw = 0, ah = 0;
    const unsigned char *pixels = FontBuildAtlas(aw, ah);
    {
        TextureDesc td;
        td.Name = "font atlas";
        td.Type = RESOURCE_DIM_TEX_2D;
        td.Width = (Uint32)aw;
        td.Height = (Uint32)ah;
        td.MipLevels = 1;
        td.Format = TEX_FORMAT_R8_UNORM;
        td.BindFlags = BIND_SHADER_RESOURCE;
        td.Usage = USAGE_IMMUTABLE;
        TextureSubResData sub;
        sub.pData = pixels;
        sub.Stride = (Uint64)aw;
        TextureData tdata;
        tdata.pSubResources = &sub;
        tdata.NumSubresources = 1;
        S.device->CreateTexture(td, &tdata, &S.atlasTex);
        S.atlasSRV = S.atlasTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    }

    // A white texture preserves the primitive materials.
    {
        const unsigned char white[] = {255,255,255,255};
        TextureDesc td;
        td.Name = "white"; td.Type = RESOURCE_DIM_TEX_2D;
        td.Width = td.Height = td.MipLevels = 1;
        td.Format = TEX_FORMAT_RGBA8_UNORM;
        td.BindFlags = BIND_SHADER_RESOURCE; td.Usage = USAGE_IMMUTABLE;
        TextureSubResData sub; sub.pData = white; sub.Stride = 4;
        TextureData data; data.pSubResources = &sub; data.NumSubresources = 1;
        S.device->CreateTexture(td, &data, &S.whiteTex);
    }
    {
        const unsigned char normal[]={128,128,255,255},material[]={160,0,255,255};
        S.neutralNormal=MakeMaterialTexture(normal,1,1,false,"neutral normal");
        S.neutralMaterial=MakeMaterialTexture(material,1,1,false,"neutral material");
        TextureDesc td;td.Name="directional shadow";td.Type=RESOURCE_DIM_TEX_2D;
        td.Width=td.Height=SHADOW_SIZE;td.Format=DEPTH_FMT;
        td.BindFlags=BIND_DEPTH_STENCIL|BIND_SHADER_RESOURCE;
        S.device->CreateTexture(td,nullptr,&S.shadowTex);
        if(!S.shadowTex)return false;
        S.shadowDSV=S.shadowTex->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        S.shadowSRV=S.shadowTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    }
    // ---- mesh pipelines ----
    S.psoDepth = MakeMeshPSO("shadow depth",false,true,false,CULL_MODE_BACK,TEX_FORMAT_UNKNOWN,DEPTH_FMT,true);
    if(!S.psoDepth)return false;
    S.psoDepth->CreateShaderResourceBinding(&S.srbDepth,true);
    S.psoOpaque    = MakeMeshPSO("opaque", true, true, false, CULL_MODE_BACK, SCENE_FMT, DEPTH_FMT);
    // Shadows write depth at a single flattened plane, so overlapping limbs are
    // rejected by the depth test and cannot double-darken. Culling is off
    // because the flattening matrix collapses winding.
    S.psoShadow    = MakeMeshPSO("shadow", false, true, true, CULL_MODE_NONE, SCENE_FMT, DEPTH_FMT);
    S.psoGround    = MakeMeshPSO("ground", false, false, true, CULL_MODE_BACK, SCENE_FMT, DEPTH_FMT);
    S.psoTrans     = MakeMeshPSO("trans", true, false, true, CULL_MODE_BACK, SCENE_FMT, DEPTH_FMT);
    S.psoTransFlat = MakeMeshPSO("transflat", false, false, true, CULL_MODE_NONE, SCENE_FMT, DEPTH_FMT);

    for (auto *pso : { S.psoOpaque.RawPtr(), S.psoShadow.RawPtr(), S.psoGround.RawPtr(),
                       S.psoTrans.RawPtr(), S.psoTransFlat.RawPtr() }) {
        if (!pso) return false;
    }
    S.psoOpaque->CreateShaderResourceBinding(&S.srbOpaque, true);
    S.psoShadow->CreateShaderResourceBinding(&S.srbShadow, true);
    S.psoGround->CreateShaderResourceBinding(&S.srbGround, true);
    S.psoTrans->CreateShaderResourceBinding(&S.srbTrans, true);
    S.psoTransFlat->CreateShaderResourceBinding(&S.srbTransFlat, true);

    for (auto *binding : {S.srbOpaque.RawPtr(), S.srbTrans.RawPtr()}) {
        binding->GetVariableByName(SHADER_TYPE_PIXEL,"g_Albedo")->Set(S.whiteTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        binding->GetVariableByName(SHADER_TYPE_PIXEL,"g_Normal")->Set(S.neutralNormal->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        binding->GetVariableByName(SHADER_TYPE_PIXEL,"g_Material")->Set(S.neutralMaterial->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    }

    // ---- line pipeline ----
    {
        std::string vsSrc = std::string(HLSL_COMMON) + HLSL_LINE_VS;
        auto vs = MakeShader(SHADER_TYPE_VERTEX, "line.vs", vsSrc);
        auto ps = MakeShader(SHADER_TYPE_PIXEL, "line.ps", HLSL_LINE_PS);
        LayoutElement elems[] = {
            LayoutElement{ 0, 0, 3, VT_FLOAT32, False },
            LayoutElement{ 1, 0, 4, VT_FLOAT32, False },
        };
        GraphicsPipelineStateCreateInfo pci;
        pci.PSODesc.Name = "line";
        pci.GraphicsPipeline.NumRenderTargets = 1;
        pci.GraphicsPipeline.RTVFormats[0] = SCENE_FMT;
        pci.GraphicsPipeline.DSVFormat = DEPTH_FMT;
        pci.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_LINE_LIST;
        pci.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
        pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = True;
        pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = False;
        pci.GraphicsPipeline.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS;
        SetBlendAlpha(pci.GraphicsPipeline.BlendDesc);
        pci.GraphicsPipeline.InputLayout.LayoutElements = elems;
        pci.GraphicsPipeline.InputLayout.NumElements = 2;
        pci.pVS = vs;
        pci.pPS = ps;
        pci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
        S.device->CreateGraphicsPipelineState(pci, &S.psoLine);
        if (!S.psoLine) return false;
        if (auto *v = S.psoLine->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Frame")) v->Set(S.cbFrame);
        S.psoLine->CreateShaderResourceBinding(&S.srbLine, true);
    }

    // ---- UI pipeline ----
    {
        auto vs = MakeShader(SHADER_TYPE_VERTEX, "ui.vs", HLSL_UI_VS);
        auto ps = MakeShader(SHADER_TYPE_PIXEL, "ui.ps", HLSL_UI_PS);
        LayoutElement elems[] = {
            LayoutElement{ 0, 0, 2, VT_FLOAT32, False },
            LayoutElement{ 1, 0, 2, VT_FLOAT32, False },
            LayoutElement{ 2, 0, 4, VT_FLOAT32, False },
        };
        SamplerDesc samp;
        samp.MinFilter = FILTER_TYPE_LINEAR;
        samp.MagFilter = FILTER_TYPE_LINEAR;
        samp.MipFilter = FILTER_TYPE_POINT;
        samp.AddressU = TEXTURE_ADDRESS_CLAMP;
        samp.AddressV = TEXTURE_ADDRESS_CLAMP;
        samp.AddressW = TEXTURE_ADDRESS_CLAMP;
        ImmutableSamplerDesc immut[] = { { SHADER_TYPE_PIXEL, "g_Atlas", samp } };
        ShaderResourceVariableDesc vars[] = {
            { SHADER_TYPE_PIXEL, "g_Atlas", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE }
        };

        auto makeUI = [&](const char *name, TEXTURE_FORMAT rtv, TEXTURE_FORMAT dsv,
                          RefCntAutoPtr<IPipelineState> &pso,
                          RefCntAutoPtr<IShaderResourceBinding> &srb) {
            GraphicsPipelineStateCreateInfo pci;
            pci.PSODesc.Name = name;
            pci.GraphicsPipeline.NumRenderTargets = 1;
            pci.GraphicsPipeline.RTVFormats[0] = rtv;
            pci.GraphicsPipeline.DSVFormat = dsv;
            pci.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            pci.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
            pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;
            pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = False;
            SetBlendAlpha(pci.GraphicsPipeline.BlendDesc);
            pci.GraphicsPipeline.InputLayout.LayoutElements = elems;
            pci.GraphicsPipeline.InputLayout.NumElements = 3;
            pci.pVS = vs;
            pci.pPS = ps;
            pci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
            pci.PSODesc.ResourceLayout.Variables = vars;
            pci.PSODesc.ResourceLayout.NumVariables = 1;
            pci.PSODesc.ResourceLayout.ImmutableSamplers = immut;
            pci.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
            S.device->CreateGraphicsPipelineState(pci, &pso);
            if (!pso) return;
            if (auto *v = pso->GetStaticVariableByName(SHADER_TYPE_VERTEX, "UIFrame")) v->Set(S.cbUI);
            pso->CreateShaderResourceBinding(&srb, true);
            if (auto *v = srb->GetVariableByName(SHADER_TYPE_PIXEL, "g_Atlas")) v->Set(S.atlasSRV);
        };
        // sky writes into the HDR scene target, which has depth bound
        makeUI("ui.sky", SCENE_FMT, DEPTH_FMT, S.psoUISky, S.srbUISky);
        makeUI("ui.hud", S.swap->GetDesc().ColorBufferFormat, TEX_FORMAT_UNKNOWN, S.psoUIHud, S.srbUIHud);
        if (!S.psoUISky || !S.psoUIHud) return false;
    }

    // ---- post pipelines ----
    {
        auto vs = MakeShader(SHADER_TYPE_VERTEX, "fs.vs", HLSL_FULLSCREEN_VS);
        SamplerDesc samp;
        samp.MinFilter = FILTER_TYPE_LINEAR;
        samp.MagFilter = FILTER_TYPE_LINEAR;
        samp.MipFilter = FILTER_TYPE_LINEAR;
        samp.AddressU = TEXTURE_ADDRESS_CLAMP;
        samp.AddressV = TEXTURE_ADDRESS_CLAMP;
        samp.AddressW = TEXTURE_ADDRESS_CLAMP;

        auto makePost = [&](const char *name, const char *psSrc, TEXTURE_FORMAT rtv,
                            bool twoTextures, RefCntAutoPtr<IPipelineState> &pso) {
            auto ps = MakeShader(SHADER_TYPE_PIXEL, name, psSrc);
            std::vector<ImmutableSamplerDesc> immut;
            std::vector<ShaderResourceVariableDesc> vars;
            immut.push_back({ SHADER_TYPE_PIXEL, "g_Scene", samp });
            vars.push_back({ SHADER_TYPE_PIXEL, "g_Scene", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE });
            if (twoTextures) {
                immut.push_back({ SHADER_TYPE_PIXEL, "g_Bloom", samp });
                vars.push_back({ SHADER_TYPE_PIXEL, "g_Bloom", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE });
            }
            static const LayoutElement fsElems[] = {
                LayoutElement{ 0, 0, 2, VT_FLOAT32, False },
                LayoutElement{ 1, 0, 2, VT_FLOAT32, False },
            };
            GraphicsPipelineStateCreateInfo pci;
            pci.PSODesc.Name = name;
            pci.GraphicsPipeline.NumRenderTargets = 1;
            pci.GraphicsPipeline.RTVFormats[0] = rtv;
            pci.GraphicsPipeline.DSVFormat = TEX_FORMAT_UNKNOWN;
            pci.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            pci.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
            pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;
            pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = False;
            pci.GraphicsPipeline.InputLayout.LayoutElements = fsElems;
            pci.GraphicsPipeline.InputLayout.NumElements = 2;
            pci.pVS = vs;
            pci.pPS = ps;
            pci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
            pci.PSODesc.ResourceLayout.Variables = vars.data();
            pci.PSODesc.ResourceLayout.NumVariables = (Uint32)vars.size();
            pci.PSODesc.ResourceLayout.ImmutableSamplers = immut.data();
            pci.PSODesc.ResourceLayout.NumImmutableSamplers = (Uint32)immut.size();
            S.device->CreateGraphicsPipelineState(pci, &pso);
        };

        makePost("bright.ps", HLSL_BRIGHT_PS, SCENE_FMT, false, S.psoBright);
        makePost("blur.ps", HLSL_BLUR_PS, SCENE_FMT, false, S.psoBlur);
        makePost("composite.ps", HLSL_COMPOSITE_PS, S.swap->GetDesc().ColorBufferFormat, true, S.psoComposite);
        if (!S.psoBright || !S.psoBlur || !S.psoComposite) return false;

        if (auto *v = S.psoBlur->GetStaticVariableByName(SHADER_TYPE_PIXEL, "BlurParams")) v->Set(S.cbBlur);
        if (auto *v = S.psoComposite->GetStaticVariableByName(SHADER_TYPE_PIXEL, "PostParams")) v->Set(S.cbPost);

        S.psoBright->CreateShaderResourceBinding(&S.srbBright, true);
        S.psoBlur->CreateShaderResourceBinding(&S.srbBlurH, true);
        S.psoBlur->CreateShaderResourceBinding(&S.srbBlurV, true);
        S.psoComposite->CreateShaderResourceBinding(&S.srbComposite, true);
    }

    CreateTargets(width, height);
    return true;
}

void Shutdown() {
    if (S.ctx) S.ctx->Flush();
    S = RState{};
}

void Resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (width == S.width && height == S.height) return;
    S.width = width;
    S.height = height;
    if (S.swap) S.swap->Resize((Uint32)width, (Uint32)height);
    CreateTargets(width, height);
}

void BeginFrame() {
    S.qOpaque.clear();
    S.qShadow.clear();
    S.qGround.clear();
    S.qTrans.clear();
    S.qTransFlat.clear();
    S.lines.clear();
    S.sky.clear();
    S.ui.clear();

    SetTarget(S.sceneRTV, S.sceneDSV, S.sceneW, S.sceneH);
    const float clear[4] = { 0.078f, 0.063f, 0.133f, 1.0f };
    S.ctx->ClearRenderTarget(S.sceneRTV, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    S.ctx->ClearDepthStencil(S.sceneDSV, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

// --- sky ---
void SkyRectGradient(float x, float y, float w, float h, Color top, Color bottom) {
    // PushUIQuad ramps left-to-right, so the vertical gradient is built by hand.
    GlyphUV uv = FontWhite();
    float4 t = ToF4(top), b = ToF4(bottom);
    UIVertex v0{ { x, y },         { uv.u0, uv.v0 }, t };
    UIVertex v1{ { x + w, y },     { uv.u0, uv.v0 }, t };
    UIVertex v2{ { x + w, y + h }, { uv.u0, uv.v0 }, b };
    UIVertex v3{ { x, y + h },     { uv.u0, uv.v0 }, b };
    S.sky.push_back(v0); S.sky.push_back(v1); S.sky.push_back(v2);
    S.sky.push_back(v0); S.sky.push_back(v2); S.sky.push_back(v3);
}

void SkyDot(float x, float y, float r, Color c) {
    PushUIQuad(S.sky, x - r, y - r, r * 2, r * 2, FontWhite(), c, c);
}

// --- 3D ---
void BeginScene(const Matrix &view, const Matrix &proj, const SceneLighting &L) {
    // The sky is a flat backdrop, so it goes down before anything writes depth.
    FlushUIBatch(S.sky, S.sceneW, S.sceneH, S.psoUISky, S.srbUISky);

    Matrix vp = Mul(view, proj);
    S.frame.vp0 = { vp._11, vp._12, vp._13, vp._14 };
    S.frame.vp1 = { vp._21, vp._22, vp._23, vp._24 };
    S.frame.vp2 = { vp._31, vp._32, vp._33, vp._34 };
    S.frame.vp3 = { vp._41, vp._42, vp._43, vp._44 };
    S.frame.viewPos = { L.viewPos.x, L.viewPos.y, L.viewPos.z, 0 };
    float3 ld = VNorm(L.lightDir);
    S.frame.lightDir = { ld.x, ld.y, ld.z, 0 };
    S.frame.keyCol = { L.keyCol.x, L.keyCol.y, L.keyCol.z, 0 };
    S.frame.skyCol = { L.skyCol.x, L.skyCol.y, L.skyCol.z, 0 };
    S.frame.gndCol = { L.gndCol.x, L.gndCol.y, L.gndCol.z, 0 };
    S.frame.rimCol = { L.rimCol.x, L.rimCol.y, L.rimCol.z, 0 };
    S.frame.fogCol = { L.fogCol.x, L.fogCol.y, L.fogCol.z, L.fogAmt };
    for (int i = 0; i < 4; i++) {
        S.frame.ptPos[i] = { L.ptPos[i].x, L.ptPos[i].y, L.ptPos[i].z, 0 };
        S.frame.ptCol[i] = { L.ptCol[i].x, L.ptCol[i].y, L.ptCol[i].z, 0 };
    }
    const bool isGL=S.deviceType==RENDER_DEVICE_TYPE_GL;
    float3 lightPos=L.focus-VNorm(L.lightDir)*40.0f;
    Matrix lightVP=MatLookAt(lightPos,L.focus,{0,1,0})*Matrix::Ortho(36,36,0.1f,85.0f,isGL);
    S.frame.lightVP0={lightVP._11,lightVP._12,lightVP._13,lightVP._14};
    S.frame.lightVP1={lightVP._21,lightVP._22,lightVP._23,lightVP._24};
    S.frame.lightVP2={lightVP._31,lightVP._32,lightVP._33,lightVP._34};
    S.frame.lightVP3={lightVP._41,lightVP._42,lightVP._43,lightVP._44};
    S.frame.shadowInfo={1.0f/SHADOW_SIZE,isGL?1.0f:-1.0f,isGL?1.0f:0.0f,S.quality>0?1.0f:0.0f};
    MapHelper<FrameCB> m(S.ctx, S.cbFrame, MAP_WRITE, MAP_FLAG_DISCARD);
    *m = S.frame;
}

bool LoadCharacterMesh(MeshId id, const CharacterAsset &asset) {
    if (id != MESH_ANIME && id != MESH_RITUAL) return false;
    if (asset.mesh.verts.empty() || asset.mesh.indices.empty() || asset.pixels.empty()) return false;
    UploadMesh(id, asset.mesh);
    auto &gm = S.meshes[id];
    gm.albedo=MakeMaterialTexture(asset.pixels.data(),asset.width,asset.height,true,"character albedo");
    gm.normal=MakeMaterialTexture(asset.normals.data(),asset.detailWidth,asset.detailHeight,false,"character normal");
    gm.material=MakeMaterialTexture(asset.material.data(),asset.detailWidth,asset.detailHeight,false,"character roughness metallic");
    if (!gm.vb || !gm.ib || !gm.albedo || !gm.normal || !gm.material) return false;
    S.psoOpaque->CreateShaderResourceBinding(&gm.srb, true);
    if (!gm.srb) return false;
    gm.srb->GetVariableByName(SHADER_TYPE_PIXEL,"g_Albedo")->Set(gm.albedo->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    gm.srb->GetVariableByName(SHADER_TYPE_PIXEL,"g_Normal")->Set(gm.normal->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    gm.srb->GetVariableByName(SHADER_TYPE_PIXEL,"g_Material")->Set(gm.material->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    return true;
}

void SetCharacterBones(int fighter, const Matrix *bones, int count) {
    if (fighter < 0 || fighter >= 2 || count > CHARACTER_BONES) return;
    for (int i = 0; i < count; ++i) {
        const Matrix &m = bones[i];
        float4 *r = &S.skinRows[(fighter * CHARACTER_BONES + i) * 4];
        r[0] = {m._11,m._12,m._13,m._14}; r[1] = {m._21,m._22,m._23,m._24};
        r[2] = {m._31,m._32,m._33,m._34}; r[3] = {m._41,m._42,m._43,m._44};
    }
}

void Mesh(MeshId m, const Matrix &model, Color c, Surface s) {
    S.qOpaque.inst[m].push_back(MakeInstance(model, c, s));
}
void Ground(MeshId m, const Matrix &model, Color c) {
    S.qGround.inst[m].push_back(MakeInstance(model, c, Surface{}));
}
void Shadow(MeshId m, const Matrix &model, Color c) {
    S.qShadow.inst[m].push_back(MakeInstance(model, c, Surface{}));
}
void Trans(MeshId m, const Matrix &model, Color c, Surface s) {
    S.qTrans.inst[m].push_back(MakeInstance(model, c, s));
}
void TransFlat(MeshId m, const Matrix &model, Color c) {
    S.qTransFlat.inst[m].push_back(MakeInstance(model, c, Surface{}));
}

void Line(float3 a, float3 b, Color c) {
    float4 f = ToF4(c);
    S.lines.push_back({ a, f });
    S.lines.push_back({ b, f });
}

void CircleY(float3 centre, float radius, Color c) {
    const int seg = 48;
    for (int i = 0; i < seg; i++) {
        float a0 = i * 2 * PI / seg, a1 = (i + 1) * 2 * PI / seg;
        Line({ centre.x + cosf(a0) * radius, centre.y, centre.z + sinf(a0) * radius },
             { centre.x + cosf(a1) * radius, centre.y, centre.z + sinf(a1) * radius }, c);
    }
}

void CircleFacing(float3 centre, float radius, float3 normal, Color c) {
    float3 n = VNorm(normal);
    float3 ref = fabsf(n.y) > 0.9f ? float3{ 1, 0, 0 } : float3{ 0, 1, 0 };
    float3 u = VNorm(VCross(ref, n)), v = VCross(n, u);
    const int seg = 36;
    for (int i = 0; i < seg; i++) {
        float a0 = i * 2 * PI / seg, a1 = (i + 1) * 2 * PI / seg;
        float3 p0 = centre + u * (cosf(a0) * radius) + v * (sinf(a0) * radius);
        float3 p1 = centre + u * (cosf(a1) * radius) + v * (sinf(a1) * radius);
        Line(p0, p1, c);
    }
}

void EndScene() {
    {
        MapHelper<float4> mapped(S.ctx, S.cbSkin, MAP_WRITE, MAP_FLAG_DISCARD);
        memcpy(mapped, S.skinRows, sizeof(S.skinRows));
    }
    // Pack every bucket into the one instance buffer, in draw order.
    std::vector<Instance> all;
    all.reserve(MAX_INSTANCES);
    auto bOpaque = BuildBatches(all, S.qOpaque);
    auto bGround = BuildBatches(all, S.qGround);
    auto bShadow = BuildBatches(all, S.qShadow);
    auto bTrans = BuildBatches(all, S.qTrans);
    auto bTransFlat = BuildBatches(all, S.qTransFlat);
    if (all.size() > MAX_INSTANCES) all.resize(MAX_INSTANCES);
    if (!all.empty()) {
        MapHelper<Instance> m(S.ctx, S.instBuf, MAP_WRITE, MAP_FLAG_DISCARD);
        memcpy(m, all.data(), all.size() * sizeof(Instance));
    }

    if(S.quality>0) {
        SetTarget(nullptr,S.shadowDSV,SHADOW_SIZE,SHADOW_SIZE);
        S.ctx->ClearDepthStencil(S.shadowDSV,CLEAR_DEPTH_FLAG,1.0f,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        DrawBatches(S.psoDepth,S.srbDepth,bOpaque);
        SetTarget(S.sceneRTV,S.sceneDSV,S.sceneW,S.sceneH);
    }
    DrawBatches(S.psoOpaque, S.srbOpaque, bOpaque);
    DrawBatches(S.psoGround, S.srbGround, bGround);
    DrawBatches(S.psoShadow, S.srbShadow, bShadow);

    if (!S.lines.empty()) {
        Uint32 n = (Uint32)S.lines.size();
        if (n > MAX_LINE_VERTS) n = MAX_LINE_VERTS;
        {
            MapHelper<LineVertex> m(S.ctx, S.lineBuf, MAP_WRITE, MAP_FLAG_DISCARD);
            memcpy(m, S.lines.data(), n * sizeof(LineVertex));
        }
        S.ctx->SetPipelineState(S.psoLine);
        S.ctx->CommitShaderResources(S.srbLine, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        IBuffer *vbs[1] = { S.lineBuf };
        Uint64 off[1] = { 0 };
        S.ctx->SetVertexBuffers(0, 1, vbs, off, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                SET_VERTEX_BUFFERS_FLAG_RESET);
        DrawAttribs da;
        da.NumVertices = n;
        da.Flags = DRAW_FLAG_VERIFY_ALL;
        S.ctx->Draw(da);
    }

    DrawBatches(S.psoTrans, S.srbTrans, bTrans);
    DrawBatches(S.psoTransFlat, S.srbTransFlat, bTransFlat);
}

void Post(const PostParams &p) {
    // bright pass into bloomA
    SetTarget(S.bloomRTVA, nullptr, S.bloomW, S.bloomH);
    FullscreenPass(S.psoBright, S.srbBright);

    // two separable blur iterations, ping-ponging A -> B -> A
    for (int pass = 0; pass < 2; pass++) {
        {
            MapHelper<BlurCB> m(S.ctx, S.cbBlur, MAP_WRITE, MAP_FLAG_DISCARD);
            m->dir = { (1.0f + pass) / S.bloomW, 0, 0, 0 };
        }
        SetTarget(S.bloomRTVB, nullptr, S.bloomW, S.bloomH);
        FullscreenPass(S.psoBlur, S.srbBlurH);
        {
            MapHelper<BlurCB> m(S.ctx, S.cbBlur, MAP_WRITE, MAP_FLAG_DISCARD);
            m->dir = { 0, (1.0f + pass) / S.bloomH, 0, 0 };
        }
        SetTarget(S.bloomRTVA, nullptr, S.bloomW, S.bloomH);
        FullscreenPass(S.psoBlur, S.srbBlurV);
    }

    {
        MapHelper<PostCB> m(S.ctx, S.cbPost, MAP_WRITE, MAP_FLAG_DISCARD);
        m->post = { p.bloom, p.vignette, p.aberration, p.grain };
        m->flash = { p.flashCol.x, p.flashCol.y, p.flashCol.z, p.flashAmt };
        m->misc = { p.time, S.deviceType == RENDER_DEVICE_TYPE_GL ? 1.0f : 0.0f, 0, 0 };
    }
    ITextureView *back = S.swap->GetCurrentBackBufferRTV();
    if (S.captureRequested) {
        // A minimized/hidden window may have a 1x1 swapchain. Capture the same
        // composite and HUD in a regular render target at the game resolution.
        CreateTarget(S.captureTarget, &back, nullptr, S.width, S.height,
                     S.swap->GetDesc().ColorBufferFormat, "capture target");
    }
    SetTarget(back, nullptr, S.width, S.height);
    FullscreenPass(S.psoComposite, S.srbComposite);
}

// --- HUD ---
void UIRect(float x, float y, float w, float h, Color c) {
    PushUIQuad(S.ui, x, y, w, h, FontWhite(), c, c);
}
void UIRectGradientH(float x, float y, float w, float h, Color left, Color right) {
    PushUIQuad(S.ui, x, y, w, h, FontWhite(), left, right);
}
void UIRectGradientV(float x, float y, float w, float h, Color top, Color bottom) {
    GlyphUV uv=FontWhite(); float4 a=ToF4(top),b=ToF4(bottom);
    UIVertex v0{{x,y},{uv.u0,uv.v0},a}, v1{{x+w,y},{uv.u0,uv.v0},a};
    UIVertex v2{{x+w,y+h},{uv.u0,uv.v0},b}, v3{{x,y+h},{uv.u0,uv.v0},b};
    S.ui.insert(S.ui.end(),{v0,v1,v2,v0,v2,v3});
}
void UISlant(float x, float y, float w, float h, float skew, Color left, Color right) {
    GlyphUV uv=FontWhite(); float4 a=ToF4(left),b=ToF4(right);
    UIVertex v0{{x+skew,y},{uv.u0,uv.v0},a}, v1{{x+w+skew,y},{uv.u0,uv.v0},b};
    UIVertex v2{{x+w,y+h},{uv.u0,uv.v0},b}, v3{{x,y+h},{uv.u0,uv.v0},a};
    S.ui.insert(S.ui.end(),{v0,v1,v2,v0,v2,v3});
}
void UITextStyled(const char *s, float x, float y, float height, Color c,
                  float widthScale, float tracking, float italic) {
    if (!s) return;
    float glyphHeight=FontHeightPx(height);
    for(const char *p=s;*p;++p) {
        float w=FontWidthPx(*p,height)*widthScale;
        if(*p!=' ') {
            GlyphUV uv=FontGlyph(*p);float4 col=ToF4(c);float skew=height*italic;
            UIVertex a{{x+skew,y},{uv.u0,uv.v0},col},b{{x+w+skew,y},{uv.u1,uv.v0},col};
            UIVertex c0{{x+w,y+glyphHeight},{uv.u1,uv.v1},col},d{{x,y+glyphHeight},{uv.u0,uv.v1},col};
            S.ui.insert(S.ui.end(),{a,b,c0,a,c0,d});
        }
        x+=w+tracking;
    }
}
void UIRectLines(float x, float y, float w, float h, float t, Color c) {
    UIRect(x, y, w, t, c);
    UIRect(x, y + h - t, w, t, c);
    UIRect(x, y + t, t, h - 2 * t, c);
    UIRect(x + w - t, y + t, t, h - 2 * t, c);
}
void UIDiamond(float x,float y,float r,Color c) {
    PushUITri(S.ui,{x,y-r},{x+r,y},{x,y+r},c);
    PushUITri(S.ui,{x,y-r},{x,y+r},{x-r,y},c);
}
void UICircle(float cx, float cy, float r, Color c) {
    const int seg = 24;
    for (int i = 0; i < seg; i++) {
        float a0 = i * 2 * PI / seg, a1 = (i + 1) * 2 * PI / seg;
        PushUITri(S.ui, { cx, cy },
                  { cx + cosf(a0) * r, cy + sinf(a0) * r },
                  { cx + cosf(a1) * r, cy + sinf(a1) * r }, c);
    }
}
void UICircleLines(float cx, float cy, float r, float t, Color c) {
    const int seg = 24;
    for (int i = 0; i < seg; i++) {
        float a0 = i * 2 * PI / seg, a1 = (i + 1) * 2 * PI / seg;
        float2 o0{ cx + cosf(a0) * r, cy + sinf(a0) * r };
        float2 o1{ cx + cosf(a1) * r, cy + sinf(a1) * r };
        float2 i0{ cx + cosf(a0) * (r - t), cy + sinf(a0) * (r - t) };
        float2 i1{ cx + cosf(a1) * (r - t), cy + sinf(a1) * (r - t) };
        PushUITri(S.ui, o0, o1, i0, c);
        PushUITri(S.ui, i0, o1, i1, c);
    }
}

void UIText(const char *s, float x, float y, int scale, Color c) {
    if (!s) return;
    float gh = (float)FontGlyphH(scale);
    for (const char *p = s; *p; x += FontAdvance(*p,scale), p++) {
        if (*p == ' ') continue;
        float gw = (float)FontGlyphW(*p,scale);
        PushUIQuad(S.ui, x, y, gw, gh, FontGlyph(*p), c, c);
    }
}

void UITextShadowed(const char *s, float x, float y, int scale, Color c) {
    Color sh = Rgb(0, 0, 0, (int)(c.a * 0.7f));
    UIText(s, x + std::max(1.0f, scale*0.15f), y + std::max(1.0f, scale*0.15f), scale, sh);
    UIText(s, x, y, scale, c);
}

void UITextCentred(const char *s, float cx, float y, int scale, Color c) {
    UITextShadowed(s, cx - FontMeasure(s, scale) * 0.5f, y, scale, c);
}

void RequestCapture() { S.captureRequested = true; }

bool TakeCapture(std::vector<unsigned char> &rgba, int &width, int &height) {
    if (S.capture.empty()) return false;
    rgba.swap(S.capture);
    width = S.captureW;
    height = S.captureH;
    S.capture.clear();
    return true;
}

// Copies the finished capture target into a staging texture and reads it back.
void CaptureBackbuffer() {
    ITexture *backTex = S.captureTarget;
    if (!backTex) return;
    TextureDesc td = backTex->GetDesc();
    td.Name = "capture";
    td.Usage = USAGE_STAGING;
    td.BindFlags = BIND_NONE;
    td.CPUAccessFlags = CPU_ACCESS_READ;
    td.MipLevels = 1;
    RefCntAutoPtr<ITexture> staging;
    S.device->CreateTexture(td, nullptr, &staging);
    if (!staging) return;

    CopyTextureAttribs cta;
    cta.pSrcTexture = backTex;
    cta.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    cta.pDstTexture = staging;
    cta.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    S.ctx->CopyTexture(cta);
    S.ctx->Flush();
    S.ctx->WaitForIdle();

    MappedTextureSubresource sub;
    S.ctx->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, sub);
    if (sub.pData) {
        int w = (int)td.Width, h = (int)td.Height;
        S.capture.resize((size_t)w * h * 4);
        const unsigned char *src = (const unsigned char *)sub.pData;
        // OpenGL stores the default framebuffer bottom-up; PNG wants top-down
        bool flip = S.deviceType == RENDER_DEVICE_TYPE_GL;
        for (int y = 0; y < h; y++) {
            int srcRow = flip ? (h - 1 - y) : y;
            memcpy(&S.capture[(size_t)y * w * 4], src + (size_t)srcRow * sub.Stride, (size_t)w * 4);
        }
        // the alpha channel is meaningless here; force it opaque
        for (size_t i = 3; i < S.capture.size(); i += 4) S.capture[i] = 255;
        S.captureW = w;
        S.captureH = h;
    }
    S.ctx->UnmapTextureSubresource(staging, 0, 0);
}

void EndFrame() {
    FlushUIBatch(S.ui, S.width, S.height, S.psoUIHud, S.srbUIHud);
    if (S.captureRequested) {
        S.captureRequested = false;
        CaptureBackbuffer();
    }
    S.swap->Present(1);
}

}  // namespace R
