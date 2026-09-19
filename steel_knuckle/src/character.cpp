#include "character.h"
#include "render.h"
#include "ufbx.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace {
// Pelvis, torso, head; then upper arm, forearm, hand, thigh, shin, foot per side.
CharacterAsset assets[2];
bool loaded[2]{};
Vector3 Vec(ufbx_vec3 v) { return {(float)v.x,(float)v.y,(float)v.z}; }
// Both exports face +Z; the combat simulation faces +X.
Vector3 Direction(Vector3 p, int) { return {p.z,p.y,-p.x}; }

float SegmentDistance(Vector3 p, Vector3 a, Vector3 b) {
    Vector3 d = b-a;
    float t = Clampf(VDot(p-a,d)/fmaxf(VDot(d,d),1e-8f),0,1);
    return VLen(p-(a+d*t));
}

void AutoWeights(MeshVertex &v, const CharacterAsset &a, int id) {
    int first = v.pos.z < 0 ? 3 : 9;
    // Limit competing bones to one limb and the torso. This keeps a skirt or
    // sleeve from being attached to the opposite side across the body.
    std::array<int,7> candidates{0,1,2,first,first+1,first+2,0};
    int count = 6;
    if (id == 1 && v.pos.x < -0.12f && v.pos.y < 0.10f) {
        // Keep the continuous trailing robe on the pelvis, away from kicking feet.
        candidates = {0,0,0,0,0,0,0}; count = 1;
    } else if (v.pos.y < -0.12f) {
        candidates = {0,first+3,first+4,first+5,0,0,0}; count = 4;
    } else if (v.pos.y > a.restA[2].y) {
        candidates = {2,1,0,0,0,0}; count = 2;
    } else if (fabsf(v.pos.z) < fabsf(a.restA[first].z) * 0.75f ||
               (id == 1 && v.pos.x < -0.10f)) {
        // Hair, the chest and back must not follow a nearby forearm.
        candidates = {0,1,2,0,0,0,0}; count = 3;
    }
    float scores[7]{}; float total = 0;
    // Strongly local weights with a smooth blend around joints.
    for (int i=0;i<count;++i) {
        int b = candidates[i];
        float d = SegmentDistance(v.pos,a.restA[b],a.restB[b]);
        scores[i] = 1.0f / powf(fmaxf(d,0.025f),4.0f);
    }
    for (int k=0;k<4;++k) {
        int best = 0; for (int i=1;i<count;++i) if (scores[i]>scores[best]) best=i;
        (&v.bones.x)[k] = float(id*CHARACTER_BONES+candidates[best]);
        (&v.weights.x)[k] = scores[best]; total += scores[best]; scores[best]=0;
    }
    if (total > 0) v.weights = v.weights / total;
}

Vector3 Joint(Vector3 a, Vector3 &b, float l1, float l2, Vector3 bend) {
    Vector3 d=b-a; float dist=VLen(d);
    if (dist<0.02f) { d={0.02f,0,0}; dist=0.02f; }
    float reach=(l1+l2)*0.999f;
    if(dist>reach) {b=a+d*(reach/dist);d=b-a;dist=reach;}
    Vector3 n=d/dist;
    float x=(l1*l1-l2*l2+dist*dist)/(2*dist);
    float h=sqrtf(fmaxf(0,l1*l1-x*x));
    Vector3 pole=bend-n*VDot(bend,n);
    if(VLen(pole)<0.001f) pole={0,1,0};
    return a+n*x+VNorm(pole)*h;
}

Matrix SegmentTransform(Vector3 a, Vector3 b, Vector3 c, Vector3 d) {
    Vector3 from=VNorm(b-a), to=VNorm(d-c);
    float dot=Clampf(VDot(from,to),-1,1);
    Matrix rot=MatIdentity();
    if(dot < -0.9999f) {
        Vector3 axis=VCross(from,Vector3{1,0,0});
        if(VLen(axis)<0.01f) axis=VCross(from,Vector3{0,0,1});
        rot=MatRotateAxis(VNorm(axis),PI);
    } else if(dot < 0.9999f) rot=MatRotateAxis(VNorm(VCross(from,to)),acosf(dot));
    // Scale only along the bone; character width and facial proportions survive.
    float ratio=VLen(d-c)/fmaxf(VLen(b-a),0.001f);
    Matrix stretch=MatIdentity();
    float v[3]={from.x,from.y,from.z};
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) stretch[i][j]+=(ratio-1)*v[i]*v[j];
    return MatTranslate(-a.x,-a.y,-a.z)*stretch*rot*MatTranslate(c.x,c.y,c.z);
}

struct VertexKey {
    uint32_t p,mat;
    int32_t nx,ny,nz,u,v;
    bool operator==(const VertexKey &k)const {
        return p==k.p&&mat==k.mat&&nx==k.nx&&ny==k.ny&&nz==k.nz&&u==k.u&&v==k.v;
    }
};
struct VertexHash { size_t operator()(const VertexKey &v)const {
    size_t h=v.p;
    for(uint32_t c:{v.mat,uint32_t(v.nx),uint32_t(v.ny),uint32_t(v.nz),uint32_t(v.u),uint32_t(v.v)})h=h*16777619u^c;
    return h;
}};
}

bool LoadCharacterAsset(const std::string &path,const std::string &texture,int id,CharacterAsset &out) {
    out=CharacterAsset{};
    ufbx_load_opts opts{};opts.target_axes=ufbx_axes_right_handed_y_up;
    opts.target_unit_meters=1;opts.generate_missing_normals=true;
    ufbx_error error{};
    std::unique_ptr<ufbx_scene,decltype(&ufbx_free_scene)> scene(ufbx_load_file(path.c_str(),&opts,&error),ufbx_free_scene);
    if(!scene){std::fprintf(stderr,"[character] %s: %s\n",path.c_str(),error.description.data);return false;}

    Vector3 lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
    for(auto *n:scene->nodes) if(n->mesh && n->mesh->num_triangles) {
        for(auto p:n->mesh->vertices) {
            Vector3 v=Vec(ufbx_transform_position(&n->geometry_to_world,p));
            for(int k=0;k<3;++k){lo[k]=std::min(lo[k],v[k]);hi[k]=std::max(hi[k],v[k]);}
        }
    }
    float height=hi.y-lo.y;
    if(!std::isfinite(height)||height<1e-6f) return false;
    float scale=(id==0?1.85f:1.90f)/height;
    Vector3 root = id==0?Vector3{0,370.164f,-4.185f}:Vector3{0,0.56f,0.015f};
    auto norm=[&](Vector3 p){return Direction((p-root)*scale,id);};
    auto sourceBone=[&](const char *name, Vector3 fallback) {
        auto *node=ufbx_find_node(scene.get(),name);
        return node?Vec(node->node_to_world.cols[3]):fallback;
    };
    Vector3 neck=id==0?sourceBone("neck",{0,532,-14}):Vector3{0,0.858f,0.015f};
    Vector3 chest=id==0?sourceBone("spine",{0,431,-4}):Vector3{0,0.73f,0.015f};
    out.restA[0]=norm(root);out.restB[0]=norm(root+Vector3{0,height*0.1f,0});
    out.restA[1]=norm(root);out.restB[1]=norm(neck);
    out.restA[2]=norm(neck);out.restB[2]=norm(neck+Vector3{0,height*0.16f,0});
    (void)chest;
    for(int side=0;side<2;++side) {
        int b=3+side*6;int sign=side==0?-1:1;
        float sourceSign=float(-sign);
        const char *prefix=sourceSign>0?"l_":"r_";
        auto bone=[&](const char *suffix,Vector3 fallback){return id==0?sourceBone((std::string(prefix)+suffix).c_str(),fallback):fallback;};
        Vector3 sh=norm(bone("shoulder",{sourceSign*0.084f,0.803f,0.015f}));
        Vector3 el=norm(bone("elbow",{sourceSign*0.119f,0.688f,0.020f}));
        Vector3 wr=norm(bone("wrist",{sourceSign*0.166f,0.590f,0.030f}));
        Vector3 hand=wr+VNorm(wr-el)*(id==0?0.11f:0.14f);
        Vector3 hip=norm(bone("thigh",{sourceSign*0.052f,0.540f,0.015f}));
        Vector3 knee=norm(bone("knee",{sourceSign*0.058f,0.300f,0.020f}));
        Vector3 ankle=norm(bone("ankle",{sourceSign*0.063f,0.060f,0.025f}));
        out.restA[b]=sh;out.restB[b]=el;
        out.restA[b+1]=el;out.restB[b+1]=wr;
        out.restA[b+2]=wr;out.restB[b+2]=hand;
        out.restA[b+3]=hip;out.restB[b+3]=knee;
        out.restA[b+4]=knee;out.restB[b+4]=ankle;
        out.restA[b+5]=ankle;out.restB[b+5]=ankle+Vector3{0.12f,0,0};
    }
    out.restB[15]={0,1,0};

    for(auto *node:scene->nodes) {
        auto *m=node->mesh;if(!m||!m->num_triangles)continue;
        auto *skin=m->skin_deformers.count?m->skin_deformers[0]:nullptr;
        std::vector<uint32_t> triangles(m->max_face_triangles*3);
        std::unordered_map<VertexKey,uint32_t,VertexHash> vertices;
        for(size_t fi=0;fi<m->faces.count;++fi) {
            auto face=m->faces[fi];if(face.num_indices<3)continue;
            uint32_t matIndex=m->face_material.count?m->face_material[fi]:0;
            bool outline=matIndex<node->materials.count && std::string(node->materials[matIndex]->name.data)=="Borde";
            // Borde is an inverted hull used for outlines. The lit mesh already
            // carries its silhouette; importing this shell would cover its face.
            if(outline)continue;
            uint32_t count=ufbx_triangulate_face(triangles.data(),triangles.size(),m,face)*3;
            for(uint32_t k=0;k<count;++k) {
                uint32_t ix=triangles[k];
                // Some sculpt exports repeat normal/UV entries for every face.
                // Weld equal attributes while preserving UV and hard-normal seams.
                auto normal=ufbx_get_vertex_vec3(&m->vertex_normal,ix);
                ufbx_vec2 uv=m->vertex_uv.exists?ufbx_get_vertex_vec2(&m->vertex_uv,ix):ufbx_vec2{};
                auto quant=[](double value){return int32_t(std::round(value*100000.0));};
                VertexKey key{m->vertex_indices[ix],matIndex,quant(normal.x),quant(normal.y),quant(normal.z),quant(uv.x),quant(uv.y)};
                auto found=vertices.find(key);
                if(found!=vertices.end()){out.mesh.indices.push_back(found->second);continue;}
                auto matrix=skin?ufbx_get_skin_vertex_matrix(skin,key.p,&node->geometry_to_world):node->geometry_to_world;
                MeshVertex v;
                v.pos=norm(Vec(ufbx_transform_position(&matrix,m->vertices[key.p])));
                auto normalMatrix=ufbx_matrix_for_normals(&matrix);
                v.normal=VNorm(Direction(Vec(ufbx_transform_direction(&normalMatrix,ufbx_get_vertex_vec3(&m->vertex_normal,ix))),id));
                if(m->vertex_uv.exists){auto uv=ufbx_get_vertex_vec2(&m->vertex_uv,ix);v.uv={(float)uv.x,1.0f-(float)uv.y};}
                AutoWeights(v,out,id);
                auto index=(uint32_t)out.mesh.verts.size();vertices.emplace(key,index);
                out.mesh.verts.push_back(v);out.mesh.indices.push_back(index);
            }
        }
    }
    int channels=0;unsigned char *pixels=nullptr;
    if(!texture.empty()) pixels=stbi_load(texture.c_str(),&out.width,&out.height,&channels,4);
    else {
        // Take the material's base-colour connection, never a normal or roughness map.
        for(auto *mat:scene->materials) {
            auto *tex=mat->pbr.base_color.texture;
            if(!tex)tex=mat->fbx.diffuse_color.texture;
            if(!tex)continue;
            if(tex->content.size)pixels=stbi_load_from_memory((const unsigned char*)tex->content.data,(int)tex->content.size,&out.width,&out.height,&channels,4);
            else pixels=stbi_load(tex->filename.data,&out.width,&out.height,&channels,4);
            if(pixels)break;
        }
    }
    if(!pixels){std::fprintf(stderr,"[character] missing albedo for %s: %s\n",path.c_str(),stbi_failure_reason());return false;}
    out.pixels.assign(pixels,pixels+(size_t)out.width*out.height*4);stbi_image_free(pixels);
    // Use the authored PBR maps when present. Pack roughness/metallic into one
    // linear texture; data maps must never receive the albedo's sRGB conversion.
    out.detailWidth = out.detailHeight = id == 1 ? 2048 : 1;
    const size_t detailSize = size_t(out.detailWidth) * out.detailHeight * 4;
    out.normals.resize(detailSize); out.material.resize(detailSize);
    for (size_t i=0; i<detailSize; i+=4) {
        out.normals[i]=128; out.normals[i+1]=128; out.normals[i+2]=255; out.normals[i+3]=255;
        out.material[i]=id==0?170:150; out.material[i+1]=0;
        out.material[i+2]=255; out.material[i+3]=255;
    }
    auto loadDetail = [&](ufbx_texture *tex, bool normal, int channel) {
        if (!tex) return;
        int w=0,h=0,c=0;
        auto *data = tex->content.size ? stbi_load_from_memory((const unsigned char*)tex->content.data,
            (int)tex->content.size,&w,&h,&c,4) : stbi_load(tex->filename.data,&w,&h,&c,4);
        if (!data) return;
        for (int y=0;y<out.detailHeight;++y) for (int x=0;x<out.detailWidth;++x) {
            size_t dst=(size_t(y)*out.detailWidth+x)*4;
            size_t src=(size_t(y*h/out.detailHeight)*w+x*w/out.detailWidth)*4;
            if(normal) for(int k=0;k<3;++k) out.normals[dst+k]=data[src+k];
            else out.material[dst+channel]=data[src];
        }
        stbi_image_free(data);
    };
    if (id == 1) for (auto *mat:scene->materials) {
        loadDetail(mat->pbr.normal_map.texture, true, 0);
        loadDetail(mat->pbr.roughness.texture, false, 0);
        loadDetail(mat->pbr.metalness.texture, false, 1);
    }
    std::printf("[character] loaded %s: %zu vertices, %zu triangles, %dx%d albedo\n",path.c_str(),out.mesh.verts.size(),out.mesh.indices.size()/3,out.width,out.height);
    return !out.mesh.indices.empty();
}

void CharacterPose(const CharacterAsset &a,const Fighter &f,Matrix *bones) {
    Vector3 dstA[CHARACTER_BONES]{},dstB[CHARACTER_BONES]{};
    Vector3 up{sinf(f.pose.lean),cosf(f.pose.lean),0};
    dstB[0]=up*VLen(a.restB[0]-a.restA[0]);
    dstB[1]=up*0.66f;dstA[2]=dstB[1];dstB[2]=dstA[2]+up*VLen(a.restB[2]-a.restA[2]);
    for(int side=0;side<2;++side) {
        int s=side==0?-1:1,b=3+side*6;
        Vector3 sh=up*0.50f+Vector3{0,-0.02f,0.21f*s};
        Vector3 hand=sh+(side==0?f.pose.lHand:f.pose.rHand);
        Vector3 elbow=Joint(sh,hand,0.30f,0.30f,{-0.2f,-1,0.5f*s});
        dstA[b]=sh;dstB[b]=elbow;dstA[b+1]=elbow;dstB[b+1]=hand;
        dstA[b+2]=hand;dstB[b+2]=hand+VNorm(hand-elbow)*VLen(a.restB[b+2]-a.restA[b+2]);
        Vector3 hip{0,-0.05f,0.11f*s};
        Vector3 foot=hip+(side==0?f.pose.lFoot:f.pose.rFoot);
        Vector3 knee=Joint(hip,foot,0.47f,0.47f,{1,0.1f,0.25f*s});
        dstA[b+3]=hip;dstB[b+3]=knee;dstA[b+4]=knee;dstB[b+4]=foot;
        dstA[b+5]=foot;dstB[b+5]=foot+Vector3{0.12f,0,0};
    }
    for(int i=0;i<15;++i)bones[i]=SegmentTransform(a.restA[i],a.restB[i],dstA[i],dstB[i]);
    bones[15]=MatIdentity();
}

bool CharactersInit(const char *directory) {
    std::filesystem::path root(directory);
    const char *paths[]={"91-anime_character/Anime_character.fbx","RITUAL+WOMEN+500k.fbx"};
    for(int i=0;i<2;++i) {
        std::string tex=i==0?(root/"91-anime_character/textures.png").string():"";
        loaded[i]=LoadCharacterAsset((root/paths[i]).string(),tex,i,assets[i]) &&
                  R::LoadCharacterMesh(i==0?MESH_ANIME:MESH_RITUAL,assets[i]);
        // Only the rest skeleton stays in CPU memory after immutable GPU upload.
        assets[i].mesh=MeshData{};std::vector<unsigned char>().swap(assets[i].pixels);
        std::vector<unsigned char>().swap(assets[i].normals);
        std::vector<unsigned char>().swap(assets[i].material);
    }
    return loaded[0]&&loaded[1];
}

bool CharacterDraw(const Fighter &f,bool shadow,const Matrix &shadowProjection) {
    if(f.id<0||f.id>1||!loaded[f.id])return false;
    Matrix bones[CHARACTER_BONES];CharacterPose(assets[f.id],f,bones);
    R::SetCharacterBones(f.id,bones,CHARACTER_BONES);
    auto mesh=f.id==0?MESH_ANIME:MESH_RITUAL;
    Matrix root=FighterRoot(f);
    if(shadow)R::Shadow(mesh,Mul(root,shadowProjection),{6,4,12,150});
    else {
        float glow=f.raging?0.25f+0.15f*sinf(f.breath*3):0;
        if(f.rageFlash>0)glow=fmaxf(glow,f.rageFlash/60.0f);
        R::Mesh(mesh,root,{255,255,255,255},{0.10f,24,glow+f.flash*0.5f,0.18f});
    }
    return true;
}

void CharactersShutdown(){for(int i=0;i<2;++i){assets[i]=CharacterAsset{};loaded[i]=false;}}
