// GLES counterpart of D3D11Device.cpp. Xbox state values are mostly the original GL enums.
#include "render/gles/GLES3Device.h"
#include "render/gles/FixedFunctionShaders.h"
#include "engine/ni/NiObjects.h"
#include "render/TextureDecode.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>
#include <android/log.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <unordered_map>
#include <vector>

namespace fut::render::xd3d {
namespace {
void NotPorted(const char* what) {
    static std::set<std::string> seen;
    if (seen.insert(what).second) std::fprintf(stderr,"GLES3 limitation: %s\n",what);
}
struct LightCB {
    f32 diffuse[4], specular[4], ambient[4], pos[4], dir[4], att[4], spot[4];
};
struct DrawCB {
    // GLSL sees these row-major Xbox bytes as transposed column-major matrices;
    // the vertex shader therefore uses matrix * vector, preserving Xbox v * M.
    D3DMATRIX world, view, proj, texm[4];
    f32 vp[4], ssOffset[4];
    f32 matDiffuse[4], matAmbient[4], matSpecular[4], matEmissive[4];
    f32 ambient[4], misc0[4], misc1[4], misc2[4], misc3[4], fogParams[4], fogColor[4], tfactor[4], alphaTest[4];
    s32 tci[4][4], colorOp[4][4], alphaOp[4][4], stageMisc[4][4], lightCount[4];
    LightCB lights[8];
    f32 texScale[4][4];
};

struct CVertex {                                      // the canonical vertex the input layout reads
    f32 pos[4], nrm[3], d0[4], d1[4], uv[4][4];
};
static_assert(sizeof(DrawCB)==1920, "std140 fixed-function uniform layout changed");
static_assert(sizeof(CVertex)==124, "canonical vertex layout changed");

void Color(u32 argb, f32 out[4]) {
    out[0] = f32((argb >> 16) & 0xFF) / 255.0f, out[1] = f32((argb >> 8) & 0xFF) / 255.0f;
    out[2] = f32(argb & 0xFF) / 255.0f, out[3] = f32(argb >> 24) / 255.0f;
}
f32 AsFloat(u32 v) {
    f32 f;
    std::memcpy(&f, &v, 4);
    return f;
}


bool DecodeXboxTexels(const Resource& r, DecodedTexture& out) {
    struct Masks { u32 a, r, g, b, bytes; };
    Masks m{};
    switch (r.format) {
    case 0x02: m = {0x8000, 0x7C00, 0x03E0, 0x001F, 2}; break;   // A1R5G5B5
    case 0x03: m = {0, 0x7C00, 0x03E0, 0x001F, 2}; break;        // X1R5G5B5
    case 0x04: m = {0xF000, 0x0F00, 0x00F0, 0x000F, 2}; break;   // A4R4G4B4
    case 0x05: m = {0, 0xF800, 0x07E0, 0x001F, 2}; break;        // R5G6B5
    case 0x06: m = {0xFF000000u, 0xFF0000, 0xFF00, 0xFF, 4}; break;   // A8R8G8B8
    case 0x07: m = {0, 0xFF0000, 0xFF00, 0xFF, 4}; break;        // X8R8G8B8
    case 0x0C: case 0x0E: case 0x0F: break;
    default: return false;
    }
    out = DecodedTexture();
    out.width = r.width, out.height = r.height;
    const bool block = r.format == 0x0C || r.format == 0x0E || r.format == 0x0F;
    if (block) out.kind = r.format == 0x0C ? DecodedTexture::Kind::BC1 : r.format == 0x0E ? DecodedTexture::Kind::BC2
                                                                                        : DecodedTexture::Kind::BC3;
    auto channel = [](u32 v, u32 mask, u8 empty) {
        if (!mask) return empty;
        u32 shift = 0;
        while (!((mask >> shift) & 1)) ++shift;
        u32 bits = 0;
        for (u32 k = mask >> shift; k & 1; k >>= 1) ++bits;
        const u32 max = (1u << bits) - 1, x = (v & mask) >> shift;
        return static_cast<u8>((x * 255 + max / 2) / max);
    };
    size_t at = 0;
    for (u32 l = 0, w = r.width, h = r.height; l < std::max(r.levels, 1u); ++l, w = std::max(w / 2, 1u),
                                                                          h = std::max(h / 2, 1u)) {
        const size_t bytes = LevelBytes(r.format, w, h);
        if (at + bytes > r.data.size()) return false;
        const u8* src = r.data.data() + at;
        if (block) {
            out.levels.emplace_back(src, src + bytes);
        } else {
            u32 xmask = 0, ymask = 0;                 // (0x001556F0)
            for (u32 k = 1, bit = 1;; k <<= 1) {
                bool used = false;
                if (k < w) xmask |= bit, bit <<= 1, used = true;
                if (k < h) ymask |= bit, bit <<= 1, used = true;
                if (!used) break;
            }
            std::vector<u8> rgba(size_t(w) * h * 4);
            u32 yoff = 0;
            for (u32 y = 0; y < h; ++y) {
                u32 xoff = 0;
                for (u32 x = 0; x < w; ++x) {
                    const u8* p = src + size_t(xoff | yoff) * m.bytes;
                    const u32 v = m.bytes == 2 ? u32(p[0] | p[1] << 8) : u32(p[0] | p[1] << 8 | p[2] << 16 | u32(p[3]) << 24);
                    u8* d = &rgba[(size_t(y) * w + x) * 4];
                    d[0] = channel(v, m.r, 0), d[1] = channel(v, m.g, 0), d[2] = channel(v, m.b, 0);
                    d[3] = channel(v, m.a, 0xFF);
                    xoff = (xoff - xmask) & xmask;
                }
                yoff = (yoff - ymask) & ymask;
            }
            out.levels.push_back(std::move(rgba));
        }
        at += bytes;
    }
    return true;
}




// S3TC is not guaranteed by GLES. Decode all BC levels on the CPU to RGBA8.
std::vector<u8> ExpandBC(const std::vector<u8>& src,u32 w,u32 h,DecodedTexture::Kind kind) {
    const bool bc1=kind==DecodedTexture::Kind::BC1,bc2=kind==DecodedTexture::Kind::BC2;
    const u32 bytes=bc1?8:16,bw=(w+3)/4,bh=(h+3)/4;
    std::vector<u8> out(size_t(w)*h*4);
    if(src.size()<size_t(bw)*bh*bytes) return out;
    auto rgb=[](u16 c,u8* o) { o[0]=u8(((c>>11)&31)*255/31);o[1]=u8(((c>>5)&63)*255/63);o[2]=u8((c&31)*255/31);o[3]=255; };
    for(u32 by=0;by<bh;++by) for(u32 bx=0;bx<bw;++bx) {
        const u8* b=src.data()+(size_t(by)*bw+bx)*bytes;
        const u8* c=b+(bc1?0:8);
        const u16 c0=u16(c[0]|c[1]<<8),c1=u16(c[2]|c[3]<<8);
        u8 palette[4][4]{};rgb(c0,palette[0]);rgb(c1,palette[1]);
        for(u32 k=0;k<3;++k) {
            palette[2][k]=u8(c0>c1||!bc1?(2*palette[0][k]+palette[1][k])/3:(palette[0][k]+palette[1][k])/2);
            palette[3][k]=u8(c0>c1||!bc1?(palette[0][k]+2*palette[1][k])/3:0);
        }
        palette[2][3]=255;palette[3][3]=c0>c1||!bc1?255:0;
        u8 alpha[8]={b[0],b[1]};
        if(!bc1&&!bc2) {
            if(b[0]>b[1]) for(u32 k=1;k<=6;++k) alpha[k+1]=u8(((7-k)*b[0]+k*b[1])/7);
            else { for(u32 k=1;k<=4;++k) alpha[k+1]=u8(((5-k)*b[0]+k*b[1])/5);alpha[6]=0;alpha[7]=255; }
        }
        u32 colorBits=0;std::memcpy(&colorBits,c+4,4);
        uint64_t alphaBits=0;if(!bc1) std::memcpy(&alphaBits,b+(bc2?0:2),bc2?8:6);
        for(u32 y=0;y<4;++y) for(u32 x=0;x<4;++x) {
            const u32 i=y*4+x,px=bx*4+x,py=by*4+y;if(px>=w||py>=h) continue;
            u8* d=out.data()+(size_t(py)*w+px)*4;std::memcpy(d,palette[(colorBits>>(i*2))&3],4);
            if(bc2) d[3]=u8(((alphaBits>>(i*4))&15)*17);
            else if(!bc1) d[3]=alpha[(alphaBits>>(i*3))&7];
        }
    }
    return out;
}
GLenum Address(u32 mode) { return mode==2?GL_MIRRORED_REPEAT:mode==3||mode==4?GL_CLAMP_TO_EDGE:GL_REPEAT; }
GLenum Blend(u32 v) {
    switch(v) {
    case 0: case 1: case 0x300: case 0x301: case 0x302: case 0x303: case 0x304: case 0x305:
    case 0x306: case 0x307: case 0x308: case 0x8001: case 0x8002: case 0x8003: case 0x8004:return v;
    default:return GL_ONE;
    }
}
GLenum AlphaBlend(GLenum v) {
    switch(v) {case GL_SRC_COLOR:return GL_SRC_ALPHA;case GL_ONE_MINUS_SRC_COLOR:return GL_ONE_MINUS_SRC_ALPHA;
    case GL_DST_COLOR:return GL_DST_ALPHA;case GL_ONE_MINUS_DST_COLOR:return GL_ONE_MINUS_DST_ALPHA;default:return v;}
}
GLenum Stencil(u32 v) {
    switch(v) {case 0:case 0x1E00:case 0x1E01:case 0x1E02:case 0x1E03:case 0x150A:case 0x8507:case 0x8508:return v;default:return GL_KEEP;}
}
void Enable(GLenum cap,bool yes) { if(yes) glEnable(cap);else glDisable(cap); }
GLuint Program(const char* vs,const char* fs,std::string& error) {
    auto compile=[&](GLenum type,const char* source) {
        GLuint sh=glCreateShader(type);glShaderSource(sh,1,&source,nullptr);glCompileShader(sh);
        GLint ok=0;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
        if(!ok) { GLint len=0;glGetShaderiv(sh,GL_INFO_LOG_LENGTH,&len);std::string log(std::max(1,len),'\0');
            glGetShaderInfoLog(sh,len,nullptr,log.data());error="GLES shader compile: "+log;glDeleteShader(sh);return GLuint(0); }
        return sh;
    };
    GLuint a=compile(GL_VERTEX_SHADER,vs);if(!a)return 0;
    GLuint b=compile(GL_FRAGMENT_SHADER,fs);if(!b){glDeleteShader(a);return 0;}
    GLuint p=glCreateProgram();glAttachShader(p,a);glAttachShader(p,b);glLinkProgram(p);glDeleteShader(a);glDeleteShader(b);
    GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){GLint len=0;glGetProgramiv(p,GL_INFO_LOG_LENGTH,&len);std::string log(std::max(1,len),'\0');
        glGetProgramInfoLog(p,len,nullptr,log.data());error="GLES program link: "+log;glDeleteProgram(p);return 0;}
    return p;
}
}

struct GLES3Device::Impl {
    EGLDisplay display=EGL_NO_DISPLAY;
    EGLContext context=EGL_NO_CONTEXT;
    EGLSurface surface=EGL_NO_SURFACE,pbuffer=EGL_NO_SURFACE;
    EGLConfig config=nullptr;
    ANativeWindow* window=nullptr;
    GLuint program=0,vao=0,vbo=0,ibo=0,ubo=0,fbo=0,white=0,present=0;
    GLuint sampler[4]{};
    struct Target { GLuint texture=0,depth=0;u32 width=0,height=0; };
    struct Tex { GLuint texture=0;u32 revision=0; };
    std::unordered_map<const Resource*,Target> targets;
    std::unordered_map<const Resource*,Tex> textures;
    std::vector<CVertex> verts;
    std::vector<u32> idx;
    ~Impl();
    Target* ColorTarget(Resource* r);
    Target* DepthTarget(Resource* r);
    bool BindTarget(Resource* c,Resource* z);
    GLuint Texture(Resource* r);
    void BindSampler(u32 stage,const u32* ts);
};

GLES3Device::Impl::~Impl() {
    if(display==EGL_NO_DISPLAY) return;
    if(context!=EGL_NO_CONTEXT) {
        eglMakeCurrent(display,pbuffer,pbuffer,context);
        for(auto& [key,t]:targets){glDeleteTextures(1,&t.texture);glDeleteRenderbuffers(1,&t.depth);}
        for(auto& [key,t]:textures)glDeleteTextures(1,&t.texture);
        glDeleteSamplers(4,sampler);glDeleteProgram(program);glDeleteProgram(present);glDeleteVertexArrays(1,&vao);
        glDeleteBuffers(1,&vbo);glDeleteBuffers(1,&ibo);glDeleteBuffers(1,&ubo);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&white);
        eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    }
    if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);
    if(pbuffer!=EGL_NO_SURFACE)eglDestroySurface(display,pbuffer);
    if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);
    if(window)ANativeWindow_release(window);
    eglTerminate(display);
}
GLES3Device::GLES3Device():impl_(std::make_unique<Impl>()){}
GLES3Device::~GLES3Device()=default;

bool GLES3Device::SetWindow(ANativeWindow* window,std::string& error) {
    Impl& d=*impl_;
    if(d.context==EGL_NO_CONTEXT){error="EGL context is not initialized";return false;}
    // onResume and surfaceChanged can announce the same producer repeatedly.
    // Creating a second EGL window surface for it fails with EGL_BAD_ALLOC.
    if(window==d.window && (window?d.surface!=EGL_NO_SURFACE:d.surface==EGL_NO_SURFACE)) {
        const EGLSurface current=window?d.surface:d.pbuffer;
        if(!eglMakeCurrent(d.display,current,current,d.context)) {
            error="eglMakeCurrent failed: "+std::to_string(eglGetError());return false;
        }
        error.clear();return true;
    }
    // Acquire before releasing the old reference: distinct ANativeWindow wrappers
    // can refer to the same Android buffer producer. Disconnect the old EGL surface
    // before trying to connect that producer again; the context stays on the pbuffer.
    if(window)ANativeWindow_acquire(window);
    if(!eglMakeCurrent(d.display,d.pbuffer,d.pbuffer,d.context)) {
        const EGLint code=eglGetError();
        if(window)ANativeWindow_release(window);
        error="eglMakeCurrent(pbuffer) failed: "+std::to_string(code);return false;
    }
    if(d.surface!=EGL_NO_SURFACE && !eglDestroySurface(d.display,d.surface)) {
        const EGLint code=eglGetError();
        if(window)ANativeWindow_release(window);
        error="eglDestroySurface failed: "+std::to_string(code);return false;
    }
    d.surface=EGL_NO_SURFACE;
    if(d.window)ANativeWindow_release(d.window);
    d.window=nullptr;
    if(!window){error.clear();return true;}
    EGLint visual=0;
    if(!eglGetConfigAttrib(d.display,d.config,EGL_NATIVE_VISUAL_ID,&visual)) {
        error="eglGetConfigAttrib failed: "+std::to_string(eglGetError());ANativeWindow_release(window);return false;
    }
    const int geometry=ANativeWindow_setBuffersGeometry(window,0,0,visual);
    if(geometry<0) {
        error="ANativeWindow_setBuffersGeometry failed: "+std::to_string(geometry);ANativeWindow_release(window);return false;
    }
    const EGLSurface next=eglCreateWindowSurface(d.display,d.config,window,nullptr);
    if(next==EGL_NO_SURFACE) {
        error="eglCreateWindowSurface failed: "+std::to_string(eglGetError());ANativeWindow_release(window);return false;
    }
    if(!eglMakeCurrent(d.display,next,next,d.context)) {
        const EGLint code=eglGetError();eglDestroySurface(d.display,next);ANativeWindow_release(window);
        error="eglMakeCurrent(window) failed: "+std::to_string(code);return false;
    }
    d.window=window;d.surface=next;
    eglSwapInterval(d.display,1);
    error.clear();
    return true;
}
bool GLES3Device::Init(ANativeWindow* window,std::string& error) {
    Impl& d=*impl_;if(d.context!=EGL_NO_CONTEXT)return SetWindow(window,error);
    auto fail=[&](const char* msg){error=std::string(msg)+": "+std::to_string(eglGetError());return false;};
    d.display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(d.display==EGL_NO_DISPLAY||!eglInitialize(d.display,nullptr,nullptr))return fail("eglInitialize");
    if(!eglBindAPI(EGL_OPENGL_ES_API))return fail("eglBindAPI");
    const EGLint attr[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT|EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT_KHR,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLint n=0;if(!eglChooseConfig(d.display,attr,&d.config,1,&n)||n==0)return fail("No GLES3 EGL config");
    const EGLint ctx[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    d.context=eglCreateContext(d.display,d.config,EGL_NO_CONTEXT,ctx);
    if(d.context==EGL_NO_CONTEXT)return fail("eglCreateContext");
    const EGLint pb[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};d.pbuffer=eglCreatePbufferSurface(d.display,d.config,pb);
    if(d.pbuffer==EGL_NO_SURFACE)return fail("eglCreatePbufferSurface");
    if(!SetWindow(window,error))return false;
    d.program=Program(kVertexShader,kFragmentShader,error);if(!d.program)return false;
    __android_log_print(ANDROID_LOG_INFO,"FuturamaGL","GPU=%s GLES=%s",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    constexpr char presentVS[]=R"(#version 300 es
    precision highp float;
    out vec2 uv;
    void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.0-1.0,0,1);})";
    constexpr char presentFS[]=R"(#version 300 es
    precision highp float;
    in vec2 uv;uniform sampler2D image;out vec4 color;
    // The Xbox backbuffer is X8R8G8B8: its unused alpha must not make the Android surface transparent.
    void main(){color=vec4(texture(image,uv).rgb,1.0);})";
    d.present=Program(presentVS,presentFS,error);if(!d.present)return false;
    glGenVertexArrays(1,&d.vao);glBindVertexArray(d.vao);glGenBuffers(1,&d.vbo);glGenBuffers(1,&d.ibo);glGenBuffers(1,&d.ubo);
    glBindBuffer(GL_ARRAY_BUFFER,d.vbo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,d.ibo);
    const size_t offsets[]={offsetof(CVertex,pos),offsetof(CVertex,nrm),offsetof(CVertex,d0),offsetof(CVertex,d1),
        offsetof(CVertex,uv[0]),offsetof(CVertex,uv[1]),offsetof(CVertex,uv[2]),offsetof(CVertex,uv[3])};
    for(GLuint i=0;i<8;++i){glEnableVertexAttribArray(i);glVertexAttribPointer(i,i==1?3:4,GL_FLOAT,GL_FALSE,sizeof(CVertex),reinterpret_cast<void*>(offsets[i]));}
    glUseProgram(d.program);glUniformBlockBinding(d.program,glGetUniformBlockIndex(d.program,"Draw"),0);
    for(int i=0;i<4;++i){std::string name="tex"+std::to_string(i);glUniform1i(glGetUniformLocation(d.program,name.c_str()),i);}
    glUseProgram(d.present);glUniform1i(glGetUniformLocation(d.present,"image"),0);
    glGenFramebuffers(1,&d.fbo);glGenSamplers(4,d.sampler);glGenTextures(1,&d.white);glBindTexture(GL_TEXTURE_2D,d.white);
    const u32 pixel=0xFFFFFFFFu;glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,&pixel);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    if(!d.BindTarget(backBuffer(),depthBuffer())){error="GLES framebuffer incomplete";return false;}
    return true;
}

GLES3Device::Impl::Target* GLES3Device::Impl::ColorTarget(Resource* r) {
    if(!r)return nullptr;Resource* key=r->parent?r->parent:r;
    auto it=targets.find(key);if(it!=targets.end())return &it->second;
    Target t;t.width=std::max(1u,key->width);t.height=std::max(1u,key->height);
    glGenTextures(1,&t.texture);glBindTexture(GL_TEXTURE_2D,t.texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,t.width,t.height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    // Rendered Xbox textures expose one level; keep mipmapped sampler states complete.
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    return &(targets[key]=t);
}
GLES3Device::Impl::Target* GLES3Device::Impl::DepthTarget(Resource* r) {
    if(!r)return nullptr;auto it=targets.find(r);if(it!=targets.end())return &it->second;
    Target t;t.width=std::max(1u,r->width);t.height=std::max(1u,r->height);
    glGenRenderbuffers(1,&t.depth);glBindRenderbuffer(GL_RENDERBUFFER,t.depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,t.width,t.height);
    return &(targets[r]=t);
}
bool GLES3Device::Impl::BindTarget(Resource* c,Resource* z) {
    Target* ct=ColorTarget(c);Target* dt=DepthTarget(z);if(!ct)return false;
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,ct->texture,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,dt?dt->depth:0);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
}

GLuint GLES3Device::Impl::Texture(Resource* r) {
    if(!r)return white;if(r->rendered){Target* t=ColorTarget(r);return t?t->texture:white;}
    auto it=textures.find(r);if(it!=textures.end()&&it->second.revision==r->revision)return it->second.texture;
    DecodedTexture dt;
    if(r->format==0x24) {
        const u32 w=r->width&~1u,h=r->height;if(!w||!h||r->data.size()<size_t(w)*h*2)return white;
        dt.width=w;dt.height=h;dt.levels.emplace_back(size_t(w)*h*4);
        auto& rgba=dt.levels[0];
        for(size_t i=0;i<size_t(w)*h;i+=2){const u8* s=r->data.data()+i*2;u8* d=rgba.data()+i*4;
            d[0]=s[0];d[1]=s[1];d[2]=s[3];d[3]=255;d[4]=s[2];d[5]=s[1];d[6]=s[3];d[7]=255;}
    } else {
        const auto* src=static_cast<const ni::NiObject*>(r->source);
        const auto* st=dynamic_cast<const ni::NiSourceTexture*>(src);
        const bool xbox=r->levels&&!std::all_of(r->data.begin(),r->data.end(),[](u8 v){return v==0;})&&DecodeXboxTexels(*r,dt);
        if(!xbox&&(!st||!st->pixelDataObj||!DecodePixelData(*st->pixelDataObj,dt))){NotPorted("texture without decodable pixel data (white)");return white;}
    }
    Tex& t=textures[r];if(!t.texture)glGenTextures(1,&t.texture);t.revision=r->revision;glBindTexture(GL_TEXTURE_2D,t.texture);
    u32 w=dt.width,h=dt.height;
    for(size_t level=0;level<dt.levels.size();++level,w=std::max(1u,w/2),h=std::max(1u,h/2)) {
        std::vector<u8> expanded;const u8* pixels=dt.levels[level].data();
        if(dt.kind!=DecodedTexture::Kind::RGBA8){expanded=ExpandBC(dt.levels[level],w,h,dt.kind);pixels=expanded.data();}
        glTexImage2D(GL_TEXTURE_2D,level,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    }
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,0);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,dt.levels.size()-1);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    return t.texture;
}
void GLES3Device::Impl::BindSampler(u32 stage,const u32* ts) {
    const GLuint s=sampler[stage];const bool minL=ts[TSS_MINFILTER]>=2,magL=ts[TSS_MAGFILTER]>=2,mipL=ts[TSS_MIPFILTER]>=2;
    const GLenum min=ts[TSS_MIPFILTER]==0?(minL?GL_LINEAR:GL_NEAREST):
        mipL?(minL?GL_LINEAR_MIPMAP_LINEAR:GL_NEAREST_MIPMAP_LINEAR):(minL?GL_LINEAR_MIPMAP_NEAREST:GL_NEAREST_MIPMAP_NEAREST);
    glSamplerParameteri(s,GL_TEXTURE_MIN_FILTER,min);glSamplerParameteri(s,GL_TEXTURE_MAG_FILTER,magL?GL_LINEAR:GL_NEAREST);
    glSamplerParameteri(s,GL_TEXTURE_WRAP_S,Address(ts[TSS_ADDRESSU]));glSamplerParameteri(s,GL_TEXTURE_WRAP_T,Address(ts[TSS_ADDRESSV]));
    glSamplerParameteri(s,GL_TEXTURE_WRAP_R,Address(ts[TSS_ADDRESSW]));
    glSamplerParameterf(s,GL_TEXTURE_MIN_LOD,f32(ts[TSS_MAXMIPLEVEL]));
    glSamplerParameterf(s,GL_TEXTURE_MAX_LOD,ts[TSS_MIPFILTER]==0?f32(ts[TSS_MAXMIPLEVEL]):1000.0f);
    if(ts[TSS_ADDRESSU]==4||ts[TSS_ADDRESSV]==4)NotPorted("texture border addressing uses clamp-to-edge");
    if(ts[TSS_MIPMAPLODBIAS])NotPorted("sampler MIPMAPLODBIAS");
    glBindSampler(stage,s);
}

void GLES3Device::Clear(u32 count,const D3DRECT* rects,u32 flags,u32 color,f32 z,u32 stencil) {
    XD3DDevice::Clear(count,rects,flags,color,z,stencil);Impl& d=*impl_;if(d.context==EGL_NO_CONTEXT||!d.BindTarget(renderTarget,depthStencil))return;
    const Impl::Target* ct=d.ColorTarget(renderTarget);f32 c[4];Color(color,c);glClearColor(c[0],c[1],c[2],c[3]);glClearDepthf(z);glClearStencil(stencil);
    const u32 mask=flags&0xF0;glColorMask(mask&0x10,mask&0x20,mask&0x40,mask&0x80);glDepthMask(GL_TRUE);glStencilMask(0xFF);
    GLbitfield bits=(mask?GL_COLOR_BUFFER_BIT:0)|((flags&1)?GL_DEPTH_BUFFER_BIT:0)|((flags&2)?GL_STENCIL_BUFFER_BIT:0);
    D3DRECT viewportRect{s32(viewport.X),s32(viewport.Y),s32(viewport.X+viewport.Width),s32(viewport.Y+viewport.Height)};
    glEnable(GL_SCISSOR_TEST);
    for(u32 i=0;i<(count&&rects?count:1u);++i){const auto& r=count&&rects?rects[i]:viewportRect;
        glScissor(r.x1,s32(ct->height)-r.y2,std::max(0,r.x2-r.x1),std::max(0,r.y2-r.y1));glClear(bits);}
    glDisable(GL_SCISSOR_TEST);
}
void GLES3Device::Swap(u32 flags) {
    XD3DDevice::Swap(flags);Impl& d=*impl_;if(!d.window||d.context==EGL_NO_CONTEXT)return;
    Impl::Target* t=d.ColorTarget(backBuffer());if(!t)return;
    EGLint w=0,h=0;eglQuerySurface(d.display,d.surface,EGL_WIDTH,&w);eglQuerySurface(d.display,d.surface,EGL_HEIGHT,&h);if(w<=0||h<=0)return;
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    const int pw=std::min(w,h*4/3),ph=pw*3/4;glViewport((w-pw)/2,(h-ph)/2,pw,ph);
    glUseProgram(d.present);glBindVertexArray(d.vao);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,t->texture);glBindSampler(0,0);
    glDrawArrays(GL_TRIANGLES,0,3);
    if(!eglSwapBuffers(d.display,d.surface))NotPorted("eglSwapBuffers failed; surface must be replaced by lifecycle callback");
}

void GLES3Device::DrawVertices(u32 primitive, u32 start, u32 count) {
    XD3DDevice::DrawVertices(primitive, start, count);
    Draw(primitive, nullptr, count, start);
}

void GLES3Device::DrawIndexedVertices(u32 primitive, u32 count, const u16* data) {
    XD3DDevice::DrawIndexedVertices(primitive, count, data);
    Draw(primitive, data, count, 0);
}

void GLES3Device::Draw(u32 primitive, const u16* list, u32 count, u32 start) {
    Impl& d = *impl_;
    if (d.context == EGL_NO_CONTEXT || !count || !streamSource || !streamStride) return;
    if (pixelShader) NotPorted("GLES3Device: a combiner pixel shader program (the texture stages are used)");
    const u32 fvf = vertexShader;
    if (fvf & 0xFFFF0000u & ~0x00FF0000u) NotPorted("GLES3Device: an FVF beyond the texture-coordinate sizes");
    // the referenced vertex range
    u32 lo = start, hi = start + count - 1;
    if (list) {
        lo = 0xFFFFFFFFu, hi = 0;
        for (u32 i = 0; i < count; ++i) lo = std::min<u32>(lo, list[i]), hi = std::max<u32>(hi, list[i]);
        lo += baseVertex, hi += baseVertex;
    }
    const u32 n = hi - lo + 1;
    const std::vector<u8>& src = streamSource->data;
    if (size_t(hi + 1) * streamStride > src.size()) return;
    // the FVF layout (D3D8: XYZ 0x002 / XYZRHW 0x004, NORMAL 0x010, DIFFUSE 0x040, SPECULAR 0x080, TEXCOUNT << 8,
    // the coordinate sizes at bit 16 + 2i: 0 two, 1 three, 2 four, 3 one float)
    const u32 posKind = fvf & 0x00E;
    const bool rhw = posKind == 0x004, hasN = (fvf & 0x010) != 0, hasD = (fvf & 0x040) != 0, hasS = (fvf & 0x080) != 0;
    const u32 texCount = (fvf >> 8) & 0xF;
    u32 dims[8] = {};
    for (u32 i = 0; i < texCount && i < 8; ++i) {
        const u32 sz = (fvf >> (16 + 2 * i)) & 3;
        dims[i] = sz == 0 ? 2 : sz == 1 ? 3 : sz == 2 ? 4 : 1;
    }
    u32 required=(rhw?4u:3u)*4u+((fvf&0x020)?4u:0u)+(hasN?12u:0u)+(hasD?4u:0u)+(hasS?4u:0u);
    for(u32 t=0;t<std::min(texCount,4u);++t) required+=dims[t]*4u;
    if(required>streamStride) { NotPorted("GLES3Device: invalid FVF stride"); return; }
    d.verts.resize(n);
    for (u32 v = 0; v < n; ++v) {
        const u8* p = src.data() + size_t(lo + v) * streamStride;
        CVertex& c = d.verts[v];
        std::memset(&c, 0, sizeof c);
        const u32 pc = rhw ? 4 : 3;
        std::memcpy(c.pos, p, pc * 4);
        if (!rhw) c.pos[3] = 1;
        p += pc * 4;
        if (fvf & 0x020) p += 4;                      // (PSIZE)
        if (hasN) std::memcpy(c.nrm, p, 12), p += 12;
        if (hasD) {
            u32 a;
            std::memcpy(&a, p, 4);
            Color(a, c.d0);
            p += 4;
        }
        if (hasS) {
            u32 a;
            std::memcpy(&a, p, 4);
            Color(a, c.d1);
            p += 4;
        }
        for (u32 t = 0; t < texCount && t < 4; ++t) {
            std::memcpy(c.uv[t], p, dims[t] * 4);
            p += dims[t] * 4;
        }
    }
    // the primitive (Xbox numbering) as a D3D11 topology; fans and quads as lists
    d.idx.clear();
    auto at = [&](u32 i) -> u32 { return list ? u32(list[i]) + baseVertex - lo : start + i - lo; };
    GLenum topo = GL_TRIANGLES;
    switch (primitive) {
    case PT_POINTLIST: topo = GL_POINTS; for (u32 i = 0; i < count; ++i) d.idx.push_back(at(i)); break;
    case PT_LINELIST: topo = GL_LINES; for (u32 i = 0; i < count; ++i) d.idx.push_back(at(i)); break;
    case PT_LINESTRIP: topo = GL_LINE_STRIP; for (u32 i = 0; i < count; ++i) d.idx.push_back(at(i)); break;
    case PT_TRIANGLELIST: for (u32 i = 0; i < count; ++i) d.idx.push_back(at(i)); break;
    case PT_TRIANGLESTRIP: topo = GL_TRIANGLE_STRIP; for (u32 i = 0; i < count; ++i) d.idx.push_back(at(i)); break;
    case PT_TRIANGLEFAN:
    case PT_POLYGON:
        for (u32 i = 2; i < count; ++i) d.idx.insert(d.idx.end(), {at(0), at(i - 1), at(i)});
        break;
    case PT_QUADLIST:
        for (u32 i = 0; i + 3 < count; i += 4) d.idx.insert(d.idx.end(), {at(i), at(i + 1), at(i + 2), at(i), at(i + 2), at(i + 3)});
        break;
    case PT_LINELOOP: topo = GL_LINE_LOOP; for (u32 i=0;i<count;++i) d.idx.push_back(at(i)); break;
    case PT_QUADSTRIP: for (u32 i=0;i+3<count;i+=2) d.idx.insert(d.idx.end(), {at(i),at(i+1),at(i+3),at(i),at(i+3),at(i+2)}); break;
    default: NotPorted("GLES3Device: unknown primitive type"); return;
    }
    if (d.idx.empty()) return;

    // ---- the constants ----
    DrawCB cb{};
    cb.world = transforms[TS_WORLD], cb.view = transforms[TS_VIEW], cb.proj = transforms[TS_PROJECTION];
    for (u32 s = 0; s < 4; ++s) cb.texm[s] = transforms[TS_TEXTURE0 + s];
    cb.vp[0] = f32(viewport.X), cb.vp[1] = f32(viewport.Y), cb.vp[2] = f32(std::max(viewport.Width, 1u));
    cb.vp[3] = f32(std::max(viewport.Height, 1u));
    cb.ssOffset[0] = screenSpaceOffset[0], cb.ssOffset[1] = screenSpaceOffset[1];
    std::memcpy(cb.matDiffuse, material.diffuse, 16), std::memcpy(cb.matAmbient, material.ambient, 16);
    std::memcpy(cb.matSpecular, material.specular, 16), std::memcpy(cb.matEmissive, material.emissive, 16);
    const u32* rs = renderState;
    Color(rs[RS_AMBIENT], cb.ambient);
    cb.misc0[0] = material.power, cb.misc0[1] = f32(rs[RS_LIGHTING] != 0), cb.misc0[2] = f32(rs[RS_COLORVERTEX] != 0);
    cb.misc0[3] = f32(rs[RS_NORMALIZENORMALS] != 0);
    cb.misc1[0] = f32(rs[RS_DIFFUSEMATERIALSOURCE]), cb.misc1[1] = f32(rs[RS_AMBIENTMATERIALSOURCE]);
    cb.misc1[2] = f32(rs[RS_SPECULARMATERIALSOURCE]), cb.misc1[3] = f32(rs[RS_EMISSIVEMATERIALSOURCE]);
    cb.misc2[0] = f32(hasN), cb.misc2[1] = f32(hasD), cb.misc2[2] = f32(hasS), cb.misc2[3] = f32(rhw);
    cb.misc3[0] = f32(rs[RS_SPECULARENABLE] != 0), cb.misc3[1] = f32(rs[RS_LOCALVIEWER] != 0);
    cb.misc3[2] = f32(rs[RS_FOGENABLE] != 0);
    {   // orig: 0x0023D5C0, the XDK's lazy fog flush (x87 in the game's 53-bit precision: doubles, F() at the stores)
        const f32 fogStart = AsFloat(rs[RS_FOGSTART]), end = AsFloat(rs[RS_FOGEND]), dens = AsFloat(rs[RS_FOGDENSITY]);
        u32 gen = rs[RS_RANGEFOGENABLE] == 0 ? 2u : 1u, mode = 0;   // 2 PLANAR, 1 RADIAL; mode 0 = 0x2601 LINEAR
        f32 k0 = 1.0f, k1 = 1.0f;
        switch (rs[RS_FOGTABLEMODE]) {
        case 0:                                       // NONE: the vertex's specular alpha (SPEC_ALPHA)
            gen = 0;
            break;
        case 3: {                                     // LINEAR: FLD end; FSUB start; FDIVR 1.0 (8192 [0x00243308])
            const double sc = end == fogStart ? 8192.0 : 1.0 / (double(end) - double(fogStart));   // FCOMP; TEST AH,0x44
            k0 = f32(double(end) * sc + 1.0);         // FLD end; FMUL ST(1); FADD 1.0; FSTP
            k1 = f32(-sc);                            // FCHS; FSTP
            break;
        }
        case 1:                                       // EXP: [0x002A5F9C]
            k0 = 1.5f, k1 = f32(double(dens) * double(std::bit_cast<f32>(0xBDB8AA0Au))), mode = 1;
            break;
        default:                                      // EXP2: [0x002A5F98]
            k0 = 1.5f, k1 = f32(double(dens) * double(std::bit_cast<f32>(0xBE596D0Bu))), mode = 2;
            break;
        }
        cb.misc3[3] = f32(mode);
        cb.fogParams[0] = k0, cb.fogParams[1] = k1, cb.fogParams[2] = 0, cb.fogParams[3] = f32(gen);
    }
    Color(rs[RS_FOGCOLOR], cb.fogColor);
    Color(rs[RS_TEXTUREFACTOR], cb.tfactor);
    cb.alphaTest[0] = f32(rs[RS_ALPHATESTENABLE] != 0), cb.alphaTest[1] = f32((rs[RS_ALPHAFUNC] - 0x200) & 7);
    cb.alphaTest[2] = f32(rs[RS_ALPHAREF] & 0xFF) / 255.0f, cb.alphaTest[3] = f32(rs[RS_SHADEMODE] == 0x1D00);
    GLuint srv[4] = {};
    for (u32 s = 0; s < 4; ++s) {
        const u32* ts = textureState[s];
        const u32 tci = ts[TSS_TEXCOORDINDEX], ttf = ts[TSS_TEXTURETRANSFORMFLAGS];
        cb.tci[s][0] = s32(tci & 0xFFFF), cb.tci[s][1] = s32((tci >> 16) & 0xF);
        if ((tci >> 16) > 3) NotPorted("texture-coordinate generation mode above 3");
        cb.tci[s][2] = s32(ttf & 0xFF), cb.tci[s][3] = (ttf & 0x100) != 0;
        cb.colorOp[s][0] = s32(ts[TSS_COLOROP]), cb.colorOp[s][1] = s32(ts[TSS_COLORARG1]);
        cb.colorOp[s][2] = s32(ts[TSS_COLORARG2]), cb.colorOp[s][3] = s32(ts[TSS_COLORARG0]);
        cb.alphaOp[s][0] = s32(ts[TSS_ALPHAOP]), cb.alphaOp[s][1] = s32(ts[TSS_ALPHAARG1]);
        cb.alphaOp[s][2] = s32(ts[TSS_ALPHAARG2]), cb.alphaOp[s][3] = s32(ts[TSS_ALPHAARG0]);
        if (ts[TSS_COLOROP] == 25 || ts[TSS_COLOROP] == 26) NotPorted("bump-env ops");
        cb.stageMisc[s][0] = s32(ts[TSS_RESULTARG]);
        cb.stageMisc[s][2] = s32((tci & 0xFFFF) < 8 ? dims[tci & 0xFFFF] : 2);
        cb.texScale[s][0] = cb.texScale[s][1] = 1.0f, cb.texScale[s][2] = 0.0f;
        if (Resource* t = textures[s]) {
            cb.texScale[s][3] = t->rendered ? 1.0f : 0.0f;
            srv[s] = d.Texture(t);
            cb.stageMisc[s][1] = srv[s] != 0;
            if (t->format == 0x24 && t->width && t->height) {   // linear: texel coordinates (L: the NV2A convention)
                cb.texScale[s][0] = 1.0f / f32(t->width), cb.texScale[s][1] = 1.0f / f32(t->height);
                cb.texScale[s][2] = rs[RS_YUVENABLE] != 0 ? 1.0f : 0.0f;   // NV097_SET_CONTROL0 bit 28
            }
            d.BindSampler(s, ts);

        }
    }
    u32 nl = 0;
    for (u32 index : activeLights) {                  // orig: the lazy flush 0x0023E015: the list's first eight in
        if (nl == 8) break;                           //  slots 0..7 (0x41024 + 0x80 per slot), the rest ignored
        const D3DLIGHT8& l = lights.at(index);        // (LightEnable sets a never-set light's default)
        LightCB& o = cb.lights[nl++];
        std::memcpy(o.diffuse, l.diffuse, 16), std::memcpy(o.specular, l.specular, 16);
        std::memcpy(o.ambient, l.ambient, 16);
        const D3DMATRIX& V = transforms[TS_VIEW];
        for (int c = 0; c < 3; ++c) {
            o.pos[c] = l.position[0] * V.m[0][c] + l.position[1] * V.m[1][c] + l.position[2] * V.m[2][c] + V.m[3][c];
            o.dir[c] = l.direction[0] * V.m[0][c] + l.direction[1] * V.m[1][c] + l.direction[2] * V.m[2][c];
        }
        o.pos[3] = f32(l.type), o.dir[3] = l.range;
        o.att[0] = l.attenuation0, o.att[1] = l.attenuation1, o.att[2] = l.attenuation2, o.att[3] = l.falloff;
        o.spot[0] = std::cos(l.theta * 0.5f), o.spot[1] = std::cos(l.phi * 0.5f);
    }
    cb.lightCount[0] = s32(nl);
    cb.ssOffset[2]=AsFloat(rs[RS_POINTSIZE]);
    glBindBuffer(GL_UNIFORM_BUFFER,d.ubo);
    glBufferData(GL_UNIFORM_BUFFER,sizeof cb,&cb,GL_STREAM_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER,0,d.ubo);

    // GLES state and submission. Viewport Y is inverted relative to D3D; clip Z was converted in the VS.
    if(!d.BindTarget(renderTarget,depthStencil))return;
    const Impl::Target* ct=d.ColorTarget(renderTarget);
    glViewport(viewport.X,s32(ct->height)-s32(viewport.Y+viewport.Height),viewport.Width,viewport.Height);
    glDepthRangef(viewport.MinZ,viewport.MaxZ);glDisable(GL_SCISSOR_TEST);
    Enable(GL_BLEND,rs[RS_ALPHABLENDENABLE]!=0);const GLenum sb=Blend(rs[RS_SRCBLEND]),db=Blend(rs[RS_DESTBLEND]);
    glBlendFuncSeparate(sb,db,AlphaBlend(sb),AlphaBlend(db));
    const u32 bop=rs[RS_BLENDOP];glBlendEquation(bop>=0x8006&&bop<=0x800B&&bop!=0x8009?bop:GL_FUNC_ADD);
    f32 bf[4];Color(rs[RS_BLENDCOLOR],bf);glBlendColor(bf[0],bf[1],bf[2],bf[3]);
    const u32 cw=rs[RS_COLORWRITEENABLE];glColorMask((cw&0x10000)!=0,(cw&0x100)!=0,(cw&1)!=0,(cw&0x1000000)!=0);
    Enable(GL_DEPTH_TEST,rs[RS_ZENABLE]!=0);glDepthMask(rs[RS_ZWRITEENABLE]?GL_TRUE:GL_FALSE);glDepthFunc(GL_NEVER+((rs[RS_ZFUNC]-0x200)&7));
    Enable(GL_STENCIL_TEST,rs[RS_STENCILENABLE]!=0);glStencilMask(rs[RS_STENCILWRITEMASK]);
    glStencilFunc(GL_NEVER+((rs[RS_STENCILFUNC]-0x200)&7),rs[RS_STENCILREF]&0xFF,rs[RS_STENCILMASK]&0xFF);
    glStencilOp(Stencil(rs[RS_STENCILFAIL]),Stencil(rs[RS_STENCILZFAIL]),Stencil(rs[RS_STENCILPASS]));
    // The translated clip positions preserve visual winding. D3D11's
    // FrontCounterClockwise=FALSE therefore maps to GL_CW (including screen polygons).
    glFrontFace(GL_CW);Enable(GL_CULL_FACE,rs[RS_CULLMODE]!=0);
    glCullFace((rs[RS_CULLMODE]==0x900)!=(rs[RS_FRONTFACE]==0x901)?GL_FRONT:GL_BACK);
    Enable(GL_POLYGON_OFFSET_FILL,rs[RS_SOLIDOFFSETENABLE]!=0);glPolygonOffset(AsFloat(rs[RS_POLYGONOFFSETZSLOPESCALE]),AsFloat(rs[RS_POLYGONOFFSETZOFFSET]));
    Enable(GL_DITHER,rs[RS_DITHERENABLE]!=0);
    if(rs[RS_FILLMODE]==0x1B01 && (topo==GL_TRIANGLES||topo==GL_TRIANGLE_STRIP)) {
        std::vector<u32> edges;
        if(topo==GL_TRIANGLES) for(size_t i=0;i+2<d.idx.size();i+=3) edges.insert(edges.end(),{d.idx[i],d.idx[i+1],d.idx[i+1],d.idx[i+2],d.idx[i+2],d.idx[i]});
        else for(size_t i=2;i<d.idx.size();++i)edges.insert(edges.end(),{d.idx[i-2],d.idx[i-1],d.idx[i-1],d.idx[i],d.idx[i],d.idx[i-2]});
        d.idx=std::move(edges);topo=GL_LINES;
    }
    if(rs[RS_VERTEXBLEND])NotPorted("VERTEXBLEND (game renderer normally skins on CPU)");
    if(rs[RS_POINTSPRITEENABLE])NotPorted("point sprites");
    // ES fixes the provoking vertex to the last; D3D flat shading uses the first.
    if(rs[RS_SHADEMODE]==0x1D00) {
        std::vector<u32> flat;
        if(topo==GL_TRIANGLES) for(size_t i=0;i+2<d.idx.size();i+=3)flat.insert(flat.end(),{d.idx[i+1],d.idx[i+2],d.idx[i]});
        else if(topo==GL_TRIANGLE_STRIP) {
            for(size_t i=2;i<d.idx.size();++i) {
                if(i&1)flat.insert(flat.end(),{d.idx[i],d.idx[i-1],d.idx[i-2]});
                else flat.insert(flat.end(),{d.idx[i-1],d.idx[i],d.idx[i-2]});
            }
            topo=GL_TRIANGLES;
        } else if(topo==GL_LINES)for(size_t i=0;i+1<d.idx.size();i+=2)flat.insert(flat.end(),{d.idx[i+1],d.idx[i]});
        else if(topo==GL_LINE_STRIP||topo==GL_LINE_LOOP) {
            for(size_t i=1;i<d.idx.size();++i)flat.insert(flat.end(),{d.idx[i],d.idx[i-1]});
            if(topo==GL_LINE_LOOP&&d.idx.size()>1)flat.insert(flat.end(),{d.idx[0],d.idx.back()});
            topo=GL_LINES;
        }
        if(!flat.empty())d.idx=std::move(flat);
    }
    glUseProgram(d.program);glBindVertexArray(d.vao);
    glBindBuffer(GL_ARRAY_BUFFER,d.vbo);glBufferData(GL_ARRAY_BUFFER,d.verts.size()*sizeof(CVertex),d.verts.data(),GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,d.ibo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,d.idx.size()*sizeof(u32),d.idx.data(),GL_STREAM_DRAW);
    const Resource* currentTarget=renderTarget&&renderTarget->parent?renderTarget->parent:renderTarget;
    for(u32 s=0;s<4;++s) {glActiveTexture(GL_TEXTURE0+s);GLuint tex=srv[s]?srv[s]:d.white;
        if(textures[s]&&textures[s]==currentTarget)tex=d.white; // avoid undefined feedback from the active framebuffer
        glBindTexture(GL_TEXTURE_2D,tex);if(!textures[s])glBindSampler(s,0);}
    glDrawElements(topo,d.idx.size(),GL_UNSIGNED_INT,nullptr);++draws_;
}
}
