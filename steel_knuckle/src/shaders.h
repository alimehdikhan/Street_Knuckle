// =============================================================================
//  shaders.h - all HLSL sources. Diligent cross-compiles these to GLSL for the
//  OpenGL backend and to SPIR-V for Vulkan, so one source serves both.
//
//  Transforms arrive as explicit rows. Apply them with weighted vector sums:
//  the OpenGL converter maps HLSL matrix constructors to GLSL constructors,
//  which interpret their vector arguments as columns instead of rows.
// =============================================================================
#pragma once

// -----------------------------------------------------------------------------
// Shared constant block. One update per frame.
// -----------------------------------------------------------------------------
static const char *HLSL_COMMON = R"(
cbuffer Frame
{
    // The view-projection travels as four explicit rows rather than a float4x4.
    // Apply these rows directly to avoid backend-specific matrix construction
    // and constant-buffer packing conventions.
    float4   g_VP0;
    float4   g_VP1;
    float4   g_VP2;
    float4   g_VP3;
    float4   g_ViewPos;      // xyz = camera position
    float4   g_LightDir;     // xyz = direction the key light travels
    float4   g_KeyCol;
    float4   g_SkyCol;       // hemisphere ambient from above
    float4   g_GndCol;       // hemisphere ambient bounced off the floor
    float4   g_RimCol;
    float4   g_FogCol;       // xyz = colour, w = density
    float4   g_PtPos[4];     // torch positions
    float4   g_PtCol[4];     // torch colours, already flickered
    float4   g_LightVP0;
    float4   g_LightVP1;
    float4   g_LightVP2;
    float4   g_LightVP3;
    float4   g_ShadowInfo; // texel size, Y sign, GL depth range, enabled
};

float4 TransformRows(float4 v, float4 r0, float4 r1, float4 r2, float4 r3)
{
    return v.x * r0 + v.y * r1 + v.z * r2 + v.w * r3;
}

struct InstIn
{
    float3 Pos    : ATTRIB0;
    float3 Normal : ATTRIB1;
    float2 UV : ATTRIB8;
    float4 Tint : ATTRIB9;
    float4 Bones : ATTRIB10;
    float4 Weights : ATTRIB11;
    float4 Row0   : ATTRIB2;
    float4 Row1   : ATTRIB3;
    float4 Row2   : ATTRIB4;
    float4 Row3   : ATTRIB5;
    float4 Color  : ATTRIB6;
    float4 Surf   : ATTRIB7;   // x spec, y gloss, z emissive, w rim
};

struct MeshPSIn
{
    float4 Pos    : SV_POSITION;
    float3 World  : WORLD;
    float3 Normal : NORMAL;
    float4 Color  : COLOR;
    float4 Surf   : SURF;
    float2 UV : TEXCOORD;
    float Character : TEXCOORD1;
};
)";

// -----------------------------------------------------------------------------
// Instanced mesh vertex shader. Rows of the model matrix are orthogonal
// (scale, then rotate, then translate), so the inverse-transpose used for the
// normal is just each row divided by its own squared length.
// -----------------------------------------------------------------------------
// The OpenGL backend matches stage varyings by the name of the shader
// parameter, so the vertex output and the pixel input must both be called
// PSIn - otherwise the program fails to link.
static const char *HLSL_MESH_VS = R"(
cbuffer Skin { float4 g_Bones[128]; };
void main(in InstIn VIn, out MeshPSIn PSIn)
{
    float3 pos = VIn.Pos, normal = VIn.Normal;
    if (dot(VIn.Weights, float4(1,1,1,1)) > 0.0) {
        pos = float3(0,0,0); normal = float3(0,0,0);
        for (int i = 0; i < 4; ++i) {
            int b = int(VIn.Bones[i]) * 4;
            pos += TransformRows(float4(VIn.Pos,1), g_Bones[b], g_Bones[b+1], g_Bones[b+2], g_Bones[b+3]).xyz * VIn.Weights[i];
            float3 r0 = g_Bones[b].xyz, r1 = g_Bones[b+1].xyz, r2 = g_Bones[b+2].xyz;
            normal += (VIn.Normal.x * r0 / max(dot(r0,r0),1e-8) +
                       VIn.Normal.y * r1 / max(dot(r1,r1),1e-8) +
                       VIn.Normal.z * r2 / max(dot(r2,r2),1e-8)) * VIn.Weights[i];
        }
    }
    float4 world = TransformRows(float4(pos, 1.0),
                                 VIn.Row0, VIn.Row1, VIn.Row2, VIn.Row3);
    PSIn.World = world.xyz;
    PSIn.Pos   = TransformRows(world, g_VP0, g_VP1, g_VP2, g_VP3);
#ifdef SHADOW_PASS
    PSIn.Pos = TransformRows(world, g_LightVP0, g_LightVP1, g_LightVP2, g_LightVP3);
#endif

    float3 r0 = VIn.Row0.xyz, r1 = VIn.Row1.xyz, r2 = VIn.Row2.xyz;
    PSIn.Normal = normalize(normal.x * r0 / max(dot(r0, r0), 1e-8) +
                            normal.y * r1 / max(dot(r1, r1), 1e-8) +
                            normal.z * r2 / max(dot(r2, r2), 1e-8));
    PSIn.Color  = VIn.Color * VIn.Tint;
    PSIn.UV = VIn.UV;
    PSIn.Surf   = VIn.Surf;
    PSIn.Character = step(0.5, dot(VIn.Weights, float4(1,1,1,1)));
}
)";

// -----------------------------------------------------------------------------
// Lit pixel shader: hemisphere ambient, wrapped key, bounce fill, Blinn-Phong
// specular, four torch point lights, fresnel rim, height-attenuated fog.
// -----------------------------------------------------------------------------
static const char *HLSL_MESH_PS = R"(
Texture2D g_Albedo;
SamplerState g_Albedo_sampler;
Texture2D g_Normal;
SamplerState g_Normal_sampler;
Texture2D g_Material;
SamplerState g_Material_sampler;
Texture2D g_ShadowMap;
SamplerState g_ShadowMap_sampler;

float HashStone(float2 p) { return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453); }
float NoiseStone(float2 p) {
    float2 i=floor(p), f=frac(p); f=f*f*(3.0-2.0*f);
    return lerp(lerp(HashStone(i),HashStone(i+float2(1,0)),f.x),
                lerp(HashStone(i+float2(0,1)),HashStone(i+float2(1,1)),f.x),f.y);
}
float Shadow(float3 world, float ndl) {
    if (g_ShadowInfo.w < 0.5) return 1.0;
    float4 h=TransformRows(float4(world,1),g_LightVP0,g_LightVP1,g_LightVP2,g_LightVP3);
    float3 p=h.xyz/h.w;
    float2 uv=float2(p.x*0.5+0.5,p.y*0.5*g_ShadowInfo.y+0.5);
    float depth=lerp(p.z,p.z*0.5+0.5,g_ShadowInfo.z);
    if(min(uv.x,uv.y)<0.002 || max(uv.x,uv.y)>0.998 || depth<0 || depth>1) return 1.0;
    float bias=max(0.00016,0.00065*(1.0-ndl));
    float result=0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        float d=g_ShadowMap.Sample(g_ShadowMap_sampler,uv+float2(x,y)*g_ShadowInfo.x).r;
        result += depth-bias<=d ? 1.0 : 0.0;
    }
    return result/9.0;
}
float3 Fresnel(float hdotv,float3 f0) { return f0+(1.0-f0)*pow(1.0-hdotv,5.0); }
float3 BRDF(float3 n,float3 v,float3 l,float3 base,float rough,float metal) {
    float3 h=normalize(v+l);
    float nl=max(dot(n,l),0.0), nv=max(dot(n,v),0.001);
    float nh=max(dot(n,h),0.0), hv=max(dot(h,v),0.0);
    float a=rough*rough, a2=a*a;
    float d=a2/max(3.14159*pow(nh*nh*(a2-1.0)+1.0,2.0),0.0001);
    float k=(rough+1.0)*(rough+1.0)/8.0;
    float g=(nv/(nv*(1.0-k)+k))*(nl/(nl*(1.0-k)+k));
    float3 f=Fresnel(hv,lerp(float3(0.04,0.04,0.04),base,metal));
    return ((1.0-f)*(1.0-metal)*base/3.14159 + f*d*g/max(4.0*nv*nl,0.001))*nl;
}
void main(in MeshPSIn PSIn, out float4 OutCol : SV_TARGET)
{
    float3 n=normalize(PSIn.Normal);
    float3 v=normalize(g_ViewPos.xyz-PSIn.World);
    float3 l=-normalize(g_LightDir.xyz);
    float4 texel=g_Albedo.Sample(g_Albedo_sampler,PSIn.UV);
    if(texel.a<0.25) discard;
    float3 base=pow(max(PSIn.Color.rgb,float3(0,0,0)),float3(2.2,2.2,2.2))*texel.rgb;
    float rough=clamp(sqrt(2.0/(PSIn.Surf.y+2.0)),0.18,0.9);
    float metal=PSIn.Surf.x>0.7 ? 0.78 : 0.0;
    float ao=1.0;
    if(PSIn.Character>0.5) {
        float2 material=g_Material.Sample(g_Material_sampler,PSIn.UV).rg;
        rough=clamp(material.r,0.24,0.94); metal=material.g;
        float3 map=g_Normal.Sample(g_Normal_sampler,PSIn.UV).xyz*2.0-1.0;
        float3 dp1=ddx(PSIn.World), dp2=ddy(PSIn.World);
        float2 duv1=ddx(PSIn.UV), duv2=ddy(PSIn.UV);
        float3 p1=cross(dp2,n), p2=cross(n,dp1);
        float3 tangent=p1*duv1.x+p2*duv2.x;
        float3 bitangent=p1*duv1.y+p2*duv2.y;
        float inv=rsqrt(max(max(dot(tangent,tangent),dot(bitangent,bitangent)),1e-10));
        n=normalize(tangent*inv*map.x-bitangent*inv*map.y+n*map.z);
    }
    if(PSIn.Surf.x<0.0) {
        float2 p=PSIn.World.xz;
        if(abs(n.y)<0.5) p=float2(PSIn.World.x+PSIn.World.z,PSIn.World.y);
        float grain=NoiseStone(p*32.0);
        float veins=NoiseStone(p*3.8+NoiseStone(p*0.7)*3.0);
        base*=lerp(0.65,1.25,veins)*(0.90+0.20*grain);
        float2 tile=p/1.65;
        tile.x+=floor(tile.y)*0.5;
        float2 edge=min(frac(tile),1.0-frac(tile));
        float joint=1.0-smoothstep(0.004,0.014,min(edge.x,edge.y));
        base*=1.0-joint*0.68;
        ao=1.0-joint*0.42;
        rough=abs(PSIn.Surf.x)>0.2 ? 0.27+veins*0.15 : 0.78;
        float bump=(grain-0.5)*0.016;
        float3 dp1=ddx(PSIn.World),dp2=ddy(PSIn.World);
        float3 r1=cross(dp2,n),r2=cross(n,dp1);
        float det=dot(dp1,r1);
        if(abs(det)>1e-8) n=normalize(n-sign(det)*(ddx(bump)*r1+ddy(bump)*r2)/max(abs(det),1e-8));
    }
    float nl=max(dot(n,l),0.0);
    float visibility=Shadow(PSIn.World,nl);
    float3 ambient=lerp(g_GndCol.rgb,g_SkyCol.rgb,n.y*0.5+0.5);
    float3 c=base*ambient*ao*(1.0-metal*0.65);
    c+=BRDF(n,v,l,base,rough,metal)*g_KeyCol.rgb*visibility;
    // Broad frontal fill keeps faces readable without flattening the moon shadows.
    float3 fill=normalize(float3(v.x,0.45,v.z));
    c+=BRDF(n,v,fill,base,rough,metal)*float3(0.8,0.85,0.95);
    for(int i=0;i<4;++i) {
        float3 delta=g_PtPos[i].xyz-PSIn.World;
        float d2=max(dot(delta,delta),0.01);
        c+=BRDF(n,v,delta*rsqrt(d2),base,rough,metal)*g_PtCol[i].rgb/(1.0+d2*0.32);
    }
    float3 f=Fresnel(max(dot(n,v),0.0),lerp(float3(0.04,0.04,0.04),base,metal));
    float3 reflection=reflect(-v,n);
    c+=f*lerp(g_GndCol.rgb,g_SkyCol.rgb*1.5,saturate(reflection.y*0.5+0.5))*(1.0-rough*0.6)*ao;
    c+=g_RimCol.rgb*pow(1.0-max(dot(n,v),0.0),4.0)*PSIn.Surf.w;
    c+=base*PSIn.Surf.z;
    float distance=length(g_ViewPos.xyz-PSIn.World);
    float fog=1.0-exp(-max(distance-4.0,0.0)*g_FogCol.w*exp(-max(PSIn.World.y,0.0)*0.09));
    c=lerp(c,g_FogCol.rgb,saturate(fog));
    OutCol=float4(c,PSIn.Color.a);
}
)";

// -----------------------------------------------------------------------------
// Unlit pixel shader. Drives the planar shadow pass and anything that should
// ignore the lighting rig.
// -----------------------------------------------------------------------------
static const char *HLSL_FLAT_PS = R"(
void main(in MeshPSIn PSIn, out float4 OutCol : SV_TARGET)
{
    OutCol = PSIn.Color;
}
)";

// -----------------------------------------------------------------------------
// Coloured line / triangle batch, for floor markings and impact rings.
// -----------------------------------------------------------------------------
static const char *HLSL_LINE_VS = R"(
struct LineIn
{
    float3 Pos   : ATTRIB0;
    float4 Color : ATTRIB1;
};
struct LinePSIn
{
    float4 Pos   : SV_POSITION;
    float4 Color : COLOR;
};
void main(in LineIn VIn, out LinePSIn PSIn)
{
    PSIn.Pos   = TransformRows(float4(VIn.Pos, 1.0), g_VP0, g_VP1, g_VP2, g_VP3);
    PSIn.Color = VIn.Color;
}
)";

static const char *HLSL_LINE_PS = R"(
struct LinePSIn
{
    float4 Pos   : SV_POSITION;
    float4 Color : COLOR;
};
void main(in LinePSIn PSIn, out float4 OutCol : SV_TARGET)
{
    OutCol = PSIn.Color;
}
)";

// -----------------------------------------------------------------------------
// 2D overlay: HUD panels, bars and text. Positions arrive in pixels and the
// glyph atlas supplies coverage in its red channel; a white texel in the atlas
// lets the same pipeline draw solid rectangles.
// -----------------------------------------------------------------------------
static const char *HLSL_UI_VS = R"(
cbuffer UIFrame
{
    float4 g_Screen;    // xy = viewport size in pixels
};
struct UIIn
{
    float2 Pos   : ATTRIB0;
    float2 UV    : ATTRIB1;
    float4 Color : ATTRIB2;
};
struct UIPSIn
{
    float4 Pos   : SV_POSITION;
    float2 UV    : TEXCOORD;
    float4 Color : COLOR;
};
void main(in UIIn VIn, out UIPSIn PSIn)
{
    float2 ndc = float2(VIn.Pos.x / g_Screen.x * 2.0 - 1.0,
                        1.0 - VIn.Pos.y / g_Screen.y * 2.0);
    PSIn.Pos   = float4(ndc, 0.0, 1.0);
    PSIn.UV    = VIn.UV;
    PSIn.Color = VIn.Color;
}
)";

static const char *HLSL_UI_PS = R"(
Texture2D    g_Atlas;
SamplerState g_Atlas_sampler;
struct UIPSIn
{
    float4 Pos   : SV_POSITION;
    float2 UV    : TEXCOORD;
    float4 Color : COLOR;
};
void main(in UIPSIn PSIn, out float4 OutCol : SV_TARGET)
{
    float cover = g_Atlas.Sample(g_Atlas_sampler, PSIn.UV).r;
    OutCol = float4(PSIn.Color.rgb, PSIn.Color.a * cover);
}
)";

// -----------------------------------------------------------------------------
// Full-screen passes. The vertex shader builds an oversized triangle from the
// vertex id, so no buffer is bound.
// -----------------------------------------------------------------------------
// An explicit quad rather than an id-generated triangle: SV_VertexID does not
// survive the HLSL to GLSL conversion reliably, and a two-triangle buffer costs
// nothing.
static const char *HLSL_FULLSCREEN_VS = R"(
struct FSIn
{
    float2 Pos : ATTRIB0;
    float2 UV  : ATTRIB1;
};
struct FSOut
{
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD;
};
void main(in FSIn VIn, out FSOut PSIn)
{
    PSIn.Pos = float4(VIn.Pos, 0.0, 1.0);
    PSIn.UV  = VIn.UV;
}
)";

static const char *HLSL_BRIGHT_PS = R"(
Texture2D    g_Scene;
SamplerState g_Scene_sampler;
struct FSOut
{
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD;
};
void main(in FSOut PSIn, out float4 OutCol : SV_TARGET)
{
    float3 c = g_Scene.Sample(g_Scene_sampler, PSIn.UV).rgb;
    float lum = dot(c, float3(0.2126, 0.7152, 0.0722));
    // soft knee, so highlights ramp into the bloom instead of popping
    float k = saturate((lum - 1.0) / 1.5);
    OutCol = float4(c * k * k, 1.0);
}
)";

static const char *HLSL_BLUR_PS = R"(
Texture2D    g_Scene;
SamplerState g_Scene_sampler;
cbuffer BlurParams
{
    float4 g_Dir;    // xy = one texel step along the blur axis
};
struct FSOut
{
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD;
};
void main(in FSOut PSIn, out float4 OutCol : SV_TARGET)
{
    float w[5];
    w[0] = 0.2270; w[1] = 0.1946; w[2] = 0.1216; w[3] = 0.0540; w[4] = 0.0162;
    float3 c = g_Scene.Sample(g_Scene_sampler, PSIn.UV).rgb * w[0];
    for (int i = 1; i < 5; ++i)
    {
        c += g_Scene.Sample(g_Scene_sampler, PSIn.UV + g_Dir.xy * float(i)).rgb * w[i];
        c += g_Scene.Sample(g_Scene_sampler, PSIn.UV - g_Dir.xy * float(i)).rgb * w[i];
    }
    OutCol = float4(c, 1.0);
}
)";

static const char *HLSL_COMPOSITE_PS = R"(
Texture2D    g_Scene;
SamplerState g_Scene_sampler;
Texture2D    g_Bloom;
SamplerState g_Bloom_sampler;
cbuffer PostParams
{
    float4 g_Post;       // x bloom, y vignette, z aberration, w grain
    float4 g_Flash;      // rgb = flash colour, a = strength
    float4 g_Misc;       // x = time, y = 1 when the scene target is bottom-up (OpenGL)
};
struct FSOut
{
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD;
};
float3 ACES(float3 x)
{
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}
void main(in FSOut PSIn, out float4 OutCol : SV_TARGET)
{
    // OpenGL renders into textures bottom-up; sample accordingly so both
    // backends present the same image.
    float2 uv = float2(PSIn.UV.x, lerp(PSIn.UV.y, 1.0 - PSIn.UV.y, g_Misc.y));
    float2 off = uv - 0.5;
    float  r2  = dot(off, off);

    // chromatic aberration grows toward the corners
    float2 ca = off * g_Post.z * r2;
    float3 c;
    c.r = g_Scene.Sample(g_Scene_sampler, uv + ca).r;
    c.g = g_Scene.Sample(g_Scene_sampler, uv).g;
    c.b = g_Scene.Sample(g_Scene_sampler, uv - ca).b;

    c += g_Bloom.Sample(g_Bloom_sampler, uv).rgb * g_Post.x;
    c += g_Flash.rgb * g_Flash.a;
    c = ACES(c * 1.12);

    // grade: cool the shadows, warm the highlights, then a touch of saturation
    float lum = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = lerp(c * float3(0.94, 0.97, 1.09), c, smoothstep(0.0, 0.40, lum));
    c = lerp(c, c * float3(1.06, 1.01, 0.93), smoothstep(0.62, 1.0, lum));
    c = saturate(lerp(float3(lum, lum, lum), c, 0.97));

    c *= lerp(1.0, 1.0 - r2 * 1.45, g_Post.y);

    float g = frac(sin(dot(uv * (1.0 + g_Misc.x), float2(12.9898, 78.233))) * 43758.5453);
    c += (g - 0.5) * g_Post.w * (1.0 - abs(lum - 0.5) * 1.2);

    // The swapchain is UNORM, so encode the linear HDR result for display.
    OutCol = float4(pow(saturate(c), float3(1.0/2.2,1.0/2.2,1.0/2.2)), 1.0);
}
)";
