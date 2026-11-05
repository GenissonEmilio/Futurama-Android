#include "render/gles/GLES3Device.h"
#include "core/XbeImage.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace fut;
using namespace fut::render::xd3d;
namespace {
int failures=0;
void Check(bool ok,const char* message) { if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);++failures;} }
void Pixel(u8 r,u8 g,u8 b,const char* message) {
    u8 p[4]{};glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    Check(p[0]==r&&p[1]==g&&p[2]==b,message);
}
bool InitFormatTable() {
    // Synthetic metadata only; tests must never rely on the absent game's FormatBits table returning zero.
    std::vector<u8> bytes(0x440);
    std::memcpy(bytes.data(),"XBEH",4);
    auto put=[&](size_t at,u32 value){std::memcpy(bytes.data()+at,&value,4);};
    put(0x104,0x10000);put(0x114,0x3f03e9a5);put(0x118,0x10200);
    put(0x11c,1);put(0x120,0x10300);put(0x208,0x56550008);put(0x2a0,1);
    put(0x304,0x158864);put(0x308,0x40);put(0x30c,0x400);put(0x310,0x40);
    for(u32 format=2;format<=5;++format)bytes[0x400+format]=1;
    for(u32 format:{0x06u,0x07u,0x1eu,0x2au})bytes[0x400+format]=2;
    const bool ok=xbe::LoadGameImage(std::move(bytes))&&FormatBits(0x06)==32&&LevelBytes(0x06,2,2)==16;
    Check(ok,"synthetic format table provides safe texture fixture sizes");return ok;
}

void DifferentiatedStreams(GLES3Device& d) {
    // Keep every draw observable. Earlier arena regressions redraw one pixel identically, which cannot expose
    // one draw consuming another draw's uniforms/vertices/indices. Here all 14,400 draws own a distinct tile in
    // one of three retained targets, and no readback/finish waits for the GPU until all cycles are submitted.
    constexpr u32 cols=80,rows=60,tile=8,cycles=3,vertexCount=128;
    constexpr u32 width=cols*tile,height=rows*tile;
    struct StreamVertex {float x,y,z,w;u32 diffuse;float u,v;};
    static_assert(sizeof(StreamVertex)==28);
    std::array<Resource*,cycles> targets{};
    std::array<std::vector<u32>,cycles> expected;
    auto* vb=d.CreateVertexBuffer2(vertexCount*sizeof(StreamVertex));
    d.SetStreamSource(0,vb,sizeof(StreamVertex));d.SetVertexShader(0x144);
    d.SetScreenSpaceOffset(0,0);
    std::memset(d.renderState,0,sizeof(d.renderState));
    std::memset(d.textureState,0,sizeof(d.textureState));
    for(u32 s=0;s<4;++s){d.SetTexture(s,nullptr);d.textureState[s][TSS_COLOROP]=1;}
    auto& rs=d.renderState;
    rs[RS_COLORWRITEENABLE]=0x01010101;rs[RS_FILLMODE]=0x1b02;
    rs[RS_SRCBLEND]=1;rs[RS_BLENDOP]=0x8006;
    auto& ts=d.textureState[0];
    ts[TSS_COLORARG1]=0;ts[TSS_COLORARG2]=3; // per-vertex DIFFUSE and per-draw TEXTUREFACTOR
    ts[TSS_ALPHAOP]=2;ts[TSS_ALPHAARG1]=0;ts[TSS_RESULTARG]=1;
    std::array<StreamVertex,vertexCount> vertices{};
    for(u32 cycle=0;cycle<cycles;++cycle) {
        targets[cycle]=d.CreateStandAloneSurface(width,height,0x06);
        d.SetRenderTarget(targets[cycle],nullptr);d.SetViewport({0,0,width,height,0,1});
        d.Clear(0,nullptr,0xf0,0xff000000,1,0);
        expected[cycle].resize(cols*rows);
        for(u32 i=0;i<cols*rows;++i) {
            const u32 x=(i%cols)*tile,y=(i/cols)*tile;
            const u32 seed=i+cycle*541;
            u32 diffuse=0xff000000,factor=0xff000000,wanted=0xff000000;
            const bool subtract=(seed&1)!=0;
            for(u32 c=0;c<3;++c) {
                const u32 a=128+(seed*(17+c*6)+c*11)%64;
                const u32 b=(seed*(7+c*4)+c*13)%64;
                diffuse|=a<<(c*8);factor|=b<<(c*8);
                wanted|=(subtract?a-b:a+b)<<(c*8);
            }
            expected[cycle][i]=wanted;
            rs[RS_TEXTUREFACTOR]=factor;rs[RS_SHADEMODE]=(seed&2)?0x1d00:0x1d01;
            ts[TSS_COLOROP]=subtract?10:7; // SUBTRACT / ADD; results stay in range without saturation
            d.SetViewport({x,y,tile,tile,0,1}); // also vary both viewport uniforms and actual raster viewport
            for(auto& v:vertices)v={float(x),float(y),.5f,1,diffuse,0,0};
            vertices[1].x=float(x+tile);vertices[2].x=float(x+tile);vertices[2].y=float(y+tile);
            vertices[3].y=float(y+tile);
            std::memcpy(d.Lock2(vb,0),vertices.data(),sizeof(vertices));
            // The first triangles form the tile; remaining triangles are degenerate. Referring to vertex127
            // forces 128 canonical vertices/upload, so VBO wraps repeatedly, not just the smaller UBO arena.
            std::vector<u16> indices(129+(seed%5)*3,0);
            const u16 diagonalA[]={0,1,2,0,2,3},diagonalB[]={0,1,3,1,2,3};
            const u16* diagonal=(seed&4)?diagonalA:diagonalB;
            std::copy(diagonal,diagonal+6,indices.begin());
            std::fill(indices.end()-3,indices.end(),u16(vertexCount-1));
            d.DrawIndexedVertices(PT_TRIANGLELIST,static_cast<u32>(indices.size()),indices.data());
        }
        // Swap marks all three stream arenas for orphaning even with this test's offscreen EGL surface.
        d.Swap(0);
    }
    std::vector<u8> pixels(size_t(width)*height*4);
    u32 mismatches=0;
    for(u32 cycle=0;cycle<cycles;++cycle) {
        d.SetRenderTarget(targets[cycle],nullptr);d.SetViewport({0,0,width,height,0,1});
        d.Clear(0,nullptr,0,0,1,0); // rebind the retained FBO without touching any pixels
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        for(u32 i=0;i<cols*rows;++i) {
            const u32 x=(i%cols)*tile+tile/2,y=(i/cols)*tile+tile/2;
            const u8* p=pixels.data()+(size_t(height-1-y)*width+x)*4;
            const u32 wanted=expected[cycle][i];
            bool ok=true;
            for(u32 c=0;c<3;++c) {
                const int difference=int(p[c])-int((wanted>>((2-c)*8))&255);
                if(difference < -1||difference > 1)ok=false;
            }
            if(!ok&&mismatches++<8)
                std::fprintf(stderr,"FAIL: differentiated stream cycle %u tile %u: RGB %u,%u,%u expected %06x\n",
                             cycle,i,p[0],p[1],p[2],wanted&0xffffff);
        }
    }
    Check(!mismatches,"all 14400 differentiated draws survive UBO/VBO/IBO wraps and Swap cycles");
    d.SetRenderTarget(d.backBuffer(),d.depthBuffer());d.SetStreamSource(0,nullptr,0);d.Release(vb);
    for(auto* target:targets)d.Release(target);
}
}
int main() {
    if(!InitFormatTable())return 1;
    GLES3Device d;std::string error;
    if(!d.Init(nullptr,error)){std::fprintf(stderr,"FAIL: EGL init: %s\n",error.c_str());return 1;}
    d.SetViewport({0,0,640,480,0,1});
    d.Clear(0,nullptr,0xf0,0xffff0000,1,0);Pixel(255,0,0,"initial framebuffer clear");
    glBindFramebuffer(GL_FRAMEBUFFER,0); // presentation changes the binding, retaining its attachments
    d.Clear(0,nullptr,0xf0,0xff00ff00,1,0);Pixel(0,255,0,"cached framebuffer rebound after presentation");
    for(int i=0;i<64;++i) {
        auto* texture=d.CreateTexture2(32,32,1,1,0x06);
        auto* surface=d.GetSurfaceLevel2(texture,0);
        auto* depth=d.CreateStandAloneSurface(32,32,0x2a);
        d.SetRenderTarget(surface,depth);d.SetViewport({0,0,32,32,0,1});
        d.Clear(0,nullptr,0xf0,0xff0000ff,1,0);Pixel(0,0,255,"replacement framebuffer clear");
        GLint colorName=0,depthName=0;
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&colorName);
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&depthName);
        d.Release(texture);d.Release(surface);d.Release(depth);
        d.SetRenderTarget(d.backBuffer(),d.depthBuffer());
        Check(!glIsTexture(colorName)&&!glIsRenderbuffer(depthName),"GPU target storage deleted on final unbind");
    }
    d.SetViewport({0,0,640,480,0,1});
    d.Clear(0,nullptr,0xf0,0xff00ff00,1,0);Pixel(0,255,0,"cache invalidated after target deletion");
    struct Vertex {float x,y,z,w,u,v;};
    const Vertex vertices[]={{0,0,0.5f,1,0,0},{640,0,0.5f,1,0,0},{0,480,0.5f,1,0,0}};
    auto* vb=d.CreateVertexBuffer2(sizeof(vertices));std::memcpy(d.Lock2(vb,0),vertices,sizeof(vertices));
    d.SetStreamSource(0,vb,sizeof(Vertex));d.SetVertexShader(0x104);
    d.renderState[RS_COLORWRITEENABLE]=0x01010101;d.renderState[RS_CULLMODE]=0;
    d.renderState[RS_SRCBLEND]=1;d.renderState[RS_DESTBLEND]=0;d.renderState[RS_FILLMODE]=0x1b02;
    for(unsigned s=0;s<4;++s)d.textureState[s][TSS_COLOROP]=1;
    auto& ts=d.textureState[0];ts[TSS_COLOROP]=2;ts[TSS_COLORARG1]=2;ts[TSS_ALPHAOP]=1;
    ts[TSS_MINFILTER]=ts[TSS_MAGFILTER]=1;ts[TSS_ADDRESSU]=ts[TSS_ADDRESSV]=ts[TSS_ADDRESSW]=3;
    auto* movie=d.CreateTexture2(2,2,1,0,0x24);auto* movieSurface=d.GetSurfaceLevel2(movie,0);
    d.SetTexture(0,movie);
    for(int frame=0;frame<16;++frame) {
        const u8 value=(frame&1)?0:255;auto* data=d.LockRect(movieSurface);
        for(int i=0;i<8;i+=4){data[i]=value;data[i+1]=0;data[i+2]=value;data[i+3]=255-value;}
        ts[TSS_MINFILTER]=(frame&1)?2:1; // cached sampler must apply changes too
        d.DrawVertices(PT_TRIANGLELIST,0,3);
        Pixel(value,0,255-value,"updated video texture and sampler produce new pixels");
    }
    // Exhaust the UBO arena, then orphan all arenas at a frame boundary. Validate the final draw's data.
    for(int i=0;i<4096;++i)d.DrawVertices(PT_TRIANGLELIST,0,3);
    Pixel(0,0,255,"stream arena wrap preserves uniform/vertex/index ranges");
    d.Swap(0);d.Clear(0,nullptr,0xf0,0xff00ff00,1,0);
    d.DrawVertices(PT_TRIANGLELIST,0,3);
    Pixel(0,0,255,"new frame orphans streams before reusing their ranges");
    // A single draw may exceed an arena's initial allocation. Remaining primitives are degenerate.
    std::vector<Vertex> large(70002,vertices[0]);
    std::memcpy(large.data(),vertices,sizeof(vertices));
    auto* largeVb=d.CreateVertexBuffer2(static_cast<u32>(large.size()*sizeof(Vertex)));
    std::memcpy(d.Lock2(largeVb,0),large.data(),large.size()*sizeof(Vertex));
    d.SetStreamSource(0,largeVb,sizeof(Vertex));d.DrawVertices(PT_TRIANGLELIST,0,static_cast<u32>(large.size()));
    Pixel(0,0,255,"vertex arena grows for an oversized draw");
    d.Release(largeVb);d.SetStreamSource(0,vb,sizeof(Vertex));
    std::vector<u16> manyIndices(600000,0);manyIndices[1]=1;manyIndices[2]=2;
    d.DrawIndexedVertices(PT_TRIANGLELIST,static_cast<u32>(manyIndices.size()),manyIndices.data());
    Pixel(0,0,255,"index arena grows for an oversized draw");
    d.Release(movieSurface);d.Release(movie);d.SetTexture(0,nullptr);
    d.Release(vb);d.SetStreamSource(0,nullptr,0);
    DifferentiatedStreams(d);
    Check(glGetError()==GL_NO_ERROR,"no GLES errors after allocation, update and deletion");
    std::printf("Android GLES checks: %s\n",failures?"FAILED":"passed");return failures?1:0;
}
