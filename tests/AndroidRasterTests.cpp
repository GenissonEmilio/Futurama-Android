// Standalone GLES pixel regressions. No game image or assets are needed.
#include "render/gles/GLES3Device.h"
#include "core/XbeImage.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

using namespace fut;
using namespace fut::render::xd3d;

namespace {
constexpr u32 kSize = 32;
int failures = 0;

bool InitFormatTable() {
    // Same synthetic header/table setup as TextureConversionTests. FormatBits
    // deliberately reads this XBE table; no game code or external file is used.
    std::vector<u8> bytes(0x440);
    std::memcpy(bytes.data(), "XBEH", 4);
    auto put = [&](size_t at, u32 value) { std::memcpy(bytes.data() + at, &value, 4); };
    put(0x104, 0x10000); put(0x114, 0x3f03e9a5); put(0x118, 0x10200);
    put(0x11c, 1); put(0x120, 0x10300); put(0x208, 0x56550008); put(0x2a0, 1);
    put(0x304, 0x158864); put(0x308, 0x40); put(0x30c, 0x400); put(0x310, 0x40);
    for (u32 format = 2; format <= 5; ++format) bytes[0x400 + format] = 1; // 16 bits
    for (u32 format : {0x06u, 0x07u, 0x1eu, 0x2au}) bytes[0x400 + format] = 2; // 32 bits
    std::string error;
    if (!xbe::LoadGameImage(std::move(bytes), &error) || FormatBits(0x06) != 32 ||
        LevelBytes(0x06, 2, 2) != 16 || LevelBytes(0x06, 4, 4) != 64) {
        std::fprintf(stderr, "FAIL: synthetic format table initialization: %s\n", error.c_str());
        ++failures;
        return false;
    }
    return true;
}

struct Vertex {
    float x, y, z, rhw;
    u32 diffuse;
    float u, v;
};
static_assert(sizeof(Vertex) == 28);

D3DMATRIX Identity() {
    D3DMATRIX m{};
    for (int i = 0; i < 4; ++i) m.m[i][i] = 1;
    return m;
}

void PhysicalPixel(int x, int y, u32 expected, const char* message, int tolerance = 0) {
    u8 actual[4]{};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
    const int wanted[] = {int((expected >> 16) & 255), int((expected >> 8) & 255), int(expected & 255)};
    bool ok = true;
    for (int i = 0; i < 3; ++i) {
        const int delta = int(actual[i]) - wanted[i];
        if (delta < -tolerance || delta > tolerance) ok = false;
    }
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s: pixel (%d,%d) RGB=(%u,%u,%u), expected=(%d,%d,%d)\n",
                     message, x, y, actual[0], actual[1], actual[2], wanted[0], wanted[1], wanted[2]);
        ++failures;
    }
}

void Pixel(int x, int y, u32 expected, const char* message, int tolerance = 0) {
    // Test coordinates, like D3D screen coordinates, start at the upper left.
    PhysicalPixel(x, kSize - 1 - y, expected, message, tolerance);
}

void NoErrors(const char* scenario) {
    for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError()) {
        std::fprintf(stderr, "FAIL: %s: GLES error 0x%x\n", scenario, unsigned(error));
        ++failures;
    }
}

void ScaledBackbuffer() {
    // This context is destroyed before the ordinary fixture creates its own.
    GLES3Device d;
    std::string error;
    if (!d.Init(nullptr, error, 2.0f)) {
        std::fprintf(stderr, "FAIL: scaled EGL init: %s\n", error.c_str());
        ++failures;
        return;
    }
    d.SetViewport({0, 0, 640, 480, 0, 1});
    d.Clear(0, nullptr, 0xf3, 0xff0000ff, 1, 0);
    const D3DRECT topLeft{0, 0, 320, 240};
    d.Clear(1, &topLeft, 0xf0, 0xffff0000, 1, 0);
    PhysicalPixel(100, 800, 0xffff0000, "2x rendering scales a logical top-left clear rectangle");
    PhysicalPixel(1000, 100, 0xff0000ff, "2x clear preserves the physical bottom-right region");

    GLint depthName = 0, width = 0, height = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                         GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depthName);
    glBindRenderbuffer(GL_RENDERBUFFER, GLuint(depthName));
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);
    if (width != 1280 || height != 960) {
        std::fprintf(stderr, "FAIL: 2x backbuffer depth size is %dx%d, expected 1280x960\n", width, height);
        ++failures;
    }

    for (u32 t = 0; t < kNumTransforms; ++t) d.SetTransform(t, Identity());
    for (u32 s = 0; s < kNumStages; ++s) {
        d.textureState[s][TSS_COLOROP] = 1; // DISABLE
        d.textureState[s][TSS_ALPHAOP] = 1;
    }
    d.renderState[RS_COLORWRITEENABLE] = 0x01010101;
    d.renderState[RS_SRCBLEND] = 1;
    d.renderState[RS_DESTBLEND] = 0;
    d.renderState[RS_BLENDOP] = 0x8006;
    d.renderState[RS_SHADEMODE] = 0x1d01;
    d.renderState[RS_FILLMODE] = 0x1b02;
    d.textureState[0][TSS_COLOROP] = d.textureState[0][TSS_ALPHAOP] = 2; // SELECTARG1 DIFFUSE
    d.textureState[0][TSS_RESULTARG] = 1;
    const Vertex vertices[] = {
        {0, 0, .5f, 1, 0xffffff00, 0, 0}, {640, 0, .5f, 1, 0xffffff00, 1, 0},
        {640, 480, .5f, 1, 0xffffff00, 1, 1}, {0, 480, .5f, 1, 0xffffff00, 0, 1}
    };
    Resource* vb = d.CreateVertexBuffer2(sizeof(vertices));
    std::memcpy(d.Lock2(vb, 0), vertices, sizeof(vertices));
    d.SetStreamSource(0, vb, sizeof(Vertex));
    d.SetVertexShader(0x144); // Logical screen-space geometry keeps its 640x480 layout.
    d.DrawVertices(PT_QUADLIST, 0, 4);
    PhysicalPixel(100, 800, 0xffffff00, "logical quad covers the physical top left at 2x");
    PhysicalPixel(1000, 800, 0xffffff00, "logical quad covers the physical top right at 2x");
    PhysicalPixel(100, 100, 0xffffff00, "logical quad covers the physical bottom left at 2x");
    PhysicalPixel(1000, 100, 0xffffff00, "logical quad covers the physical bottom right at 2x");
    d.SetStreamSource(0, nullptr, 0);
    d.Release(vb);
    NoErrors("2x backbuffer clear, depth storage and logical viewport");
}

struct Fixture {
    GLES3Device d;
    Resource* color = nullptr;
    Resource* depth = nullptr;
    Resource* vb = nullptr;

    bool Init() {
        std::string error;
        if (!d.Init(nullptr, error)) {
            std::fprintf(stderr, "FAIL: EGL init: %s\n", error.c_str());
            return false;
        }
        color = d.CreateStandAloneSurface(kSize, kSize, 0x06);
        depth = d.CreateStandAloneSurface(kSize, kSize, 0x2a);
        vb = d.CreateVertexBuffer2(4 * sizeof(Vertex));
        Reset();
        NoErrors("initialization");
        return true;
    }

    ~Fixture() {
        d.SetTexture(0, nullptr);
        d.SetStreamSource(0, nullptr, 0);
        d.SetRenderTarget(d.backBuffer(), d.depthBuffer());
        d.Release(vb);
        d.Release(color);
        d.Release(depth);
    }

    void Reset() {
        std::memset(d.renderState, 0, sizeof(d.renderState));
        std::memset(d.textureState, 0, sizeof(d.textureState));
        for (u32 t = 0; t < kNumTransforms; ++t) d.SetTransform(t, Identity());
        for (u32 s = 0; s < kNumStages; ++s) {
            d.SetTexture(s, nullptr);
            d.textureState[s][TSS_COLOROP] = 1; // DISABLE
            d.textureState[s][TSS_ALPHAOP] = 1;
        }
        auto& rs = d.renderState;
        rs[RS_COLORWRITEENABLE] = 0x01010101;
        rs[RS_ZFUNC] = 0x201; // LESS
        rs[RS_ZWRITEENABLE] = 1;
        rs[RS_ALPHAFUNC] = 0x207; // ALWAYS
        rs[RS_SRCBLEND] = 1;
        rs[RS_DESTBLEND] = 0;
        rs[RS_BLENDOP] = 0x8006; // ADD
        rs[RS_SHADEMODE] = 0x1d01; // GOURAUD
        rs[RS_FILLMODE] = 0x1b02; // SOLID
        rs[RS_FRONTFACE] = 0x900; // CW
        auto& ts = d.textureState[0];
        ts[TSS_COLOROP] = ts[TSS_ALPHAOP] = 2; // SELECTARG1
        ts[TSS_COLORARG1] = ts[TSS_ALPHAARG1] = 0; // DIFFUSE
        ts[TSS_RESULTARG] = 1; // CURRENT
        ts[TSS_MINFILTER] = ts[TSS_MAGFILTER] = 1; // POINT
        ts[TSS_ADDRESSU] = ts[TSS_ADDRESSV] = ts[TSS_ADDRESSW] = 3; // CLAMP
        d.SetRenderTarget(color, depth);
        d.SetViewport({0, 0, kSize, kSize, 0, 1});
        d.SetScreenSpaceOffset(0, 0);
        d.SetStreamSource(0, vb, sizeof(Vertex));
        d.SetVertexShader(0x144); // XYZRHW | DIFFUSE | TEX1 (two-component UV)
        d.SetPixelShader(0);
        Clear();
    }

    void Clear(u32 argb = 0xff000000) { d.Clear(0, nullptr, 0xf3, argb, 1, 0); }

    void Rect(u32 argb, float z = 0.5f, float top = 0, float bottom = kSize,
              float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
        const Vertex vertices[] = {
            {0, top, z, 1, argb, u0, v0}, {float(kSize), top, z, 1, argb, u1, v0},
            {float(kSize), bottom, z, 1, argb, u1, v1}, {0, bottom, z, 1, argb, u0, v1}
        };
        std::memcpy(d.Lock2(vb, 0), vertices, sizeof(vertices));
        d.DrawVertices(PT_QUADLIST, 0, 4);
    }

    void Triangle(bool clockwise) {
        Vertex vertices[] = {{2, 2, .5f, 1, 0xffff0000, 0, 0},
                             {30, 2, .5f, 1, 0xffff0000, 0, 0},
                             {2, 30, .5f, 1, 0xffff0000, 0, 0}};
        if (!clockwise) std::swap(vertices[1], vertices[2]);
        std::memcpy(d.Lock2(vb, 0), vertices, sizeof(vertices));
        d.DrawVertices(PT_TRIANGLELIST, 0, 3);
    }
};

void Depth(Fixture& f) {
    f.Reset();
    f.d.renderState[RS_ZENABLE] = 1;
    f.Rect(0xffff0000, .75f);
    f.Rect(0xff00ff00, .25f);
    f.Rect(0xff0000ff, .5f);
    Pixel(16, 16, 0xff00ff00, "LESS keeps the nearest surface independent of submission order");

    f.Clear();
    f.Rect(0xffff0000, .75f);
    f.d.renderState[RS_ZWRITEENABLE] = 0;
    f.Rect(0xff00ff00, .25f);
    f.d.renderState[RS_ZWRITEENABLE] = 1;
    f.Rect(0xff0000ff, .5f);
    Pixel(16, 16, 0xff0000ff, "ZWRITE disabled draws color without occluding a later surface");
    NoErrors("depth and ZWRITE");
}

void Alpha(Fixture& f) {
    f.Reset();
    auto& rs = f.d.renderState;
    rs[RS_ZENABLE] = 1;
    rs[RS_ALPHATESTENABLE] = 1;
    rs[RS_ALPHAFUNC] = 0x204; // GREATER
    rs[RS_ALPHAREF] = 128;
    f.Rect(0x80ff0000, .1f);
    Pixel(16, 16, 0xff000000, "alpha exactly at GREATER reference is discarded");
    f.Rect(0xff00ff00, .5f);
    Pixel(16, 16, 0xff00ff00, "alpha discard does not write depth");

    f.Clear();
    rs[RS_ALPHAFUNC] = 0x202; // EQUAL
    f.Rect(0x80ff0000);
    Pixel(16, 16, 0xffff0000, "alpha EQUAL accepts the reference byte");

    f.Clear(0xff0000ff);
    rs[RS_ZENABLE] = 0;
    rs[RS_ALPHATESTENABLE] = 0;
    rs[RS_ALPHABLENDENABLE] = 1;
    rs[RS_SRCBLEND] = 0x302; // SRCALPHA
    rs[RS_DESTBLEND] = 0x303; // INVSRCALPHA
    f.Rect(0x80ff0000);
    Pixel(16, 16, 0xff80007f, "source alpha blends red over blue", 1);
    NoErrors("alpha test and blend");
}

void Culling(Fixture& f) {
    f.Reset();
    // Clockwise and counterclockwise here refer to the image seen from the front,
    // with (0,0) at its upper left, rather than an API's framebuffer origin.
    const struct { u32 front, cull; bool visibleCCW, visibleCW; } cases[] = {
        {0x900, 0, true, true}, {0x900, 0x900, true, false}, {0x900, 0x901, false, true},
        {0x901, 0, true, true}, {0x901, 0x900, false, true}, {0x901, 0x901, true, false}
    };
    for (const auto& test : cases) {
        for (bool clockwise : {false, true}) {
            f.Clear();
            f.d.renderState[RS_FRONTFACE] = test.front;
            f.d.renderState[RS_CULLMODE] = test.cull;
            f.Triangle(clockwise);
            const bool visible = clockwise ? test.visibleCW : test.visibleCCW;
            char message[120];
            std::snprintf(message, sizeof(message), "visual %s triangle, CULLMODE=0x%x FRONTFACE=0x%x",
                          clockwise ? "CW" : "CCW", unsigned(test.cull), unsigned(test.front));
            Pixel(8, 8, visible ? 0xffff0000 : 0xff000000, message);
        }
    }
    NoErrors("cull modes and FRONTFACE");
}

void FlatStrip(Fixture& f) {
    f.Reset();
    auto& rs = f.d.renderState;
    rs[RS_SHADEMODE] = 0x1d00; // FLAT
    rs[RS_CULLMODE] = 0x900; // The strips below are visually counterclockwise.
    Resource* vb = f.d.CreateVertexBuffer2(8 * sizeof(Vertex));
    f.d.SetStreamSource(0, vb, sizeof(Vertex));

    // The D3D/HLSL contract is [v0,v1,v2] with provoking v0, followed by
    // [v1,v3,v2] with provoking v1, even though winding changes assembly order.
    // See Microsoft DirectXShaderCompiler's SV_Barycentrics documentation,
    // "Barycentric Coordinate Ordering"; nointerpolation uses that provoking vertex.
    const Vertex colors[] = {
        {2, 2, .5f, 1, 0xffff0000, 0, 0}, {2, 30, .5f, 1, 0xff0000ff, 0, 0},
        {30, 2, .5f, 1, 0xff00ff00, 0, 0}, {30, 30, .5f, 1, 0xffffff00, 0, 0}
    };
    std::memcpy(f.d.Lock2(vb, 0), colors, sizeof(colors));
    f.d.DrawVertices(PT_TRIANGLESTRIP, 0, 4);
    Pixel(8, 8, 0xffff0000, "flat strip even triangle uses its first submitted vertex");
    Pixel(24, 24, 0xff0000ff, "flat strip odd triangle uses the next submitted vertex");

    // Reverse the index stream, preserving winding but changing both leading
    // colors. This checks indexed assembly independently of physical VB order.
    f.Clear();
    const u16 reverse[] = {3, 2, 1, 0};
    f.d.DrawIndexedVertices(PT_TRIANGLESTRIP, 4, reverse);
    Pixel(24, 24, 0xffffff00, "indexed flat strip uses the first index's color");
    Pixel(8, 8, 0xff00ff00, "indexed odd strip triangle uses the second index's color");

    // NiOutline::AddPolygonRun joins strips without degenerate positions: the
    // final two vertices of each run have alpha zero, suppressing both bridging
    // triangles by their leading vertices. They must not create a diagonal fill
    // between these disconnected black quads, even with culling enabled.
    const Vertex runs[] = {
        {2, 2, .5f, 1, 0xff000000, 0, 0}, {2, 12, .5f, 1, 0xff000000, 0, 0},
        {10, 2, .5f, 1, 0x00ffffff, 0, 0}, {10, 12, .5f, 1, 0x00ffffff, 0, 0},
        {22, 20, .5f, 1, 0xff000000, 0, 0}, {22, 30, .5f, 1, 0xff000000, 0, 0},
        {30, 20, .5f, 1, 0x00ffffff, 0, 0}, {30, 30, .5f, 1, 0x00ffffff, 0, 0}
    };
    std::memcpy(f.d.Lock2(vb, 0), runs, sizeof(runs));
    rs[RS_ALPHABLENDENABLE] = 1;
    rs[RS_SRCBLEND] = 0x302;
    rs[RS_DESTBLEND] = 0x303;
    for (u32 cull : {0u, 0x900u}) {
        rs[RS_CULLMODE] = cull;
        f.Clear(0xff00ff00);
        f.d.DrawVertices(PT_TRIANGLESTRIP, 0, 8);
        Pixel(4, 4, 0xff000000, "outline first run retains its even black triangle");
        Pixel(8, 10, 0xff000000, "outline first run retains its odd black triangle");
        Pixel(24, 22, 0xff000000, "outline second run retains its even black triangle");
        Pixel(28, 28, 0xff000000, "outline second run retains its odd black triangle");
        Pixel(16, 12, 0xff00ff00, "alpha-zero first bridge preserves the background");
        Pixel(16, 17, 0xff00ff00, "alpha-zero second bridge preserves the background");
    }
    f.d.SetStreamSource(0, f.vb, sizeof(Vertex));
    f.d.Release(vb);
    NoErrors("flat strip leading colors, winding and outline alpha bridges");
}

void PerspectiveOutlineStrip(Fixture& f) {
    f.Reset();
    struct ScreenVertex { float x, y, z; u32 diffuse; float u, v; };
    static_assert(sizeof(ScreenVertex) == 24);
    // Match DrawScreenPolygon: normalized polygon positions are multiplied by
    // eye-space depth, then projected with ScreenProjection (FVF XYZ, not RHW).
    // Keep the real outline pool's 404-vertex transparent unused tail as well.
    constexpr size_t count = 0x194;
    constexpr float nearZ = .1f, farZ = 100;
    std::vector<ScreenVertex> vertices(count);
    for (auto& v : vertices) v.z = nearZ; // PackScreenPolygon's zero-position tail.
    auto put = [&](size_t index, float x, float y, float eyeZ) {
        vertices[index] = {x / kSize * eyeZ, (1 - y / kSize) * eyeZ, eyeZ, 0, .5f, float(index & 1)};
    };
    put(0, 2, 2, 4); put(1, 2, 12, 4); put(2, 10, 2, 11); put(3, 10, 12, 11);
    put(4, 22, 20, 8); put(5, 22, 30, 8); put(6, 30, 20, 12); put(7, 30, 30, 12);
    D3DMATRIX projection{};
    projection.m[0][0] = projection.m[1][1] = 2;
    projection.m[2][0] = projection.m[2][1] = -1;
    projection.m[2][2] = farZ / (farZ - nearZ);
    projection.m[2][3] = 1;
    projection.m[3][2] = -farZ * nearZ / (farZ - nearZ);
    f.d.SetTransform(TS_PROJECTION, projection);
    f.d.SetVertexShader(0x142); // XYZ | DIFFUSE | TEX1
    Resource* vb = f.d.CreateVertexBuffer2(static_cast<u32>(vertices.size() * sizeof(ScreenVertex)));
    f.d.SetStreamSource(0, vb, sizeof(ScreenVertex));
    auto& rs = f.d.renderState;
    rs[RS_SHADEMODE] = 0x1d00;
    rs[RS_CULLMODE] = 0x900;
    rs[RS_ZENABLE] = 1;
    rs[RS_ZWRITEENABLE] = 0;
    rs[RS_ZFUNC] = 0x203; // LEQUAL, like the outline property.
    rs[RS_ALPHABLENDENABLE] = 1;
    rs[RS_SRCBLEND] = 0x302;
    rs[RS_DESTBLEND] = 0x303;
    for (int frame = 0; frame < 16; ++frame) {
        const u32 color = (frame & 1) ? 0xffff00ff : 0xff000000;
        for (size_t index : {size_t(0), size_t(1), size_t(4), size_t(5)}) vertices[index].diffuse = color;
        std::memcpy(f.d.Lock2(vb, 0), vertices.data(), vertices.size() * sizeof(ScreenVertex));
        f.Clear(0xff00ff00);
        f.d.DrawVertices(PT_TRIANGLESTRIP, 0, static_cast<u32>(vertices.size()));
        Pixel(4, 4, color, "perspective outline keeps the even leading color across frame reuse");
        Pixel(8, 10, color, "perspective outline keeps the odd leading color across frame reuse");
        Pixel(24, 22, color, "perspective outline renders a second disconnected run");
        Pixel(28, 28, color, "perspective outline preserves winding on its second run");
        Pixel(16, 12, 0xff00ff00, "perspective alpha bridge preserves the background");
        Pixel(16, 17, 0xff00ff00, "perspective odd alpha bridge preserves the background");
        Pixel(8, 28, 0xff00ff00, "transparent unused outline tail does not fill the viewport");
        f.d.Swap(0); // Exercise the next frame's arena reuse with changed colors.
    }
    f.d.SetStreamSource(0, f.vb, sizeof(Vertex));
    f.d.Release(vb);
    NoErrors("perspective outline projection, alpha bridges and 404-vertex pool tail");
}

void RenderTexture(Fixture& f) {
    f.Reset();
    Resource* texture = f.d.CreateTexture2(kSize, kSize, 1, 1, 0x06);
    Resource* surface = f.d.GetSurfaceLevel2(texture, 0);
    f.d.SetRenderTarget(surface, nullptr);
    f.Clear();
    f.Rect(0xffff0000, .5f, 0, 16);
    f.Rect(0xff0000ff, .5f, 16, 32);
    Pixel(8, 8, 0xffff0000, "render target drawing puts red at the top");
    Pixel(8, 24, 0xff0000ff, "render target drawing puts blue at the bottom");

    f.d.SetRenderTarget(f.color, f.depth);
    f.Clear();
    f.d.SetTexture(0, texture);
    auto& ts = f.d.textureState[0];
    ts[TSS_COLORARG1] = ts[TSS_ALPHAARG1] = 2; // TEXTURE
    f.Rect(0xffffffff);
    Pixel(8, 8, 0xffff0000, "sampling a rendered texture preserves its top");
    Pixel(8, 24, 0xff0000ff, "sampling a rendered texture preserves its bottom");

    // A two-component D3D texture transform promotes (u,v) to (u,v,1).
    // Translation is in the final row of that 3x2 transform (row-vector convention).
    ts[TSS_TEXTURETRANSFORMFLAGS] = 2; // COUNT2
    D3DMATRIX uv = Identity();
    uv.m[2][1] = .5f;
    f.d.SetTransform(TS_TEXTURE0, uv);
    f.Rect(0xffffffff, .5f, 0, 32, .25f, .25f, .25f, .25f);
    Pixel(16, 16, 0xff0000ff, "UV translation moves a top-quarter sample to the bottom quarter");

    uv = Identity();
    uv.m[2][2] = .5f;
    f.d.SetTransform(TS_TEXTURE0, uv);
    ts[TSS_TEXTURETRANSFORMFLAGS] = 0x103; // COUNT3 | PROJECTED
    f.Rect(0xffffffff, .5f, 0, 32, .25f, .375f, .25f, .375f);
    Pixel(16, 16, 0xff0000ff, "projected UV divides by its final transformed component");
    f.d.SetTexture(0, nullptr);
    f.d.Release(surface);
    f.d.Release(texture);
    NoErrors("rendered texture orientation and UV transforms");
}

void WriteSolid(GLES3Device& d, Resource* texture, u32 level, u32 argb) {
    // A solid color has the same layout before and after Xbox texel swizzling.
    Resource* surface = d.GetSurfaceLevel2(texture, level);
    auto reject = [&](const char* reason) {
        std::fprintf(stderr, "FAIL: WriteSolid level %u: %s\n", unsigned(level), reason);
        ++failures;
        d.Release(surface);
    };
    if (!surface || !surface->parent || surface->parent != texture) {
        reject("missing texture surface or parent");
        return;
    }
    const Resource& parent = *surface->parent;
    if (parent.format != 0x06 || FormatBits(parent.format) != 32) {
        reject("solid ARGB writes require the initialized 32-bit format table");
        return;
    }
    size_t offset = 0;
    for (u32 mip = 0, w = parent.width, h = parent.height; mip < level;
         ++mip, w = std::max(w / 2, 1u), h = std::max(h / 2, 1u)) {
        const size_t bytes = LevelBytes(parent.format, w, h);
        if (offset > parent.data.size() || bytes > parent.data.size() - offset) {
            reject("mip offset exceeds the parent allocation");
            return;
        }
        offset += bytes;
    }
    const size_t pixelsCount = size_t(surface->width) * surface->height;
    if (pixelsCount > std::numeric_limits<size_t>::max() / sizeof(argb) ||
        offset > parent.data.size() || pixelsCount * sizeof(argb) > parent.data.size() - offset) {
        reject("solid texels exceed the parent allocation");
        return;
    }
    u8* pixels = d.LockRect(surface);
    if (!pixels || pixels != parent.data.data() + offset) {
        reject("LockRect did not return the checked mip range");
        return;
    }
    for (size_t i = 0; i < pixelsCount; ++i)
        std::memcpy(pixels + sizeof(argb) * i, &argb, sizeof(argb));
    d.Release(surface);
}

bool HasExtension(const char* name) {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const char* extension = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i)));
        if (extension && std::strcmp(extension, name) == 0) return true;
    }
    return false;
}

void TextureSampling(Fixture& f) {
    f.Reset();
    auto& ts = f.d.textureState[0];
    auto& rs = f.d.renderState;
    ts[TSS_COLORARG1] = ts[TSS_ALPHAARG1] = 2; // TEXTURE
    rs[RS_ALPHABLENDENABLE] = 1;
    rs[RS_SRCBLEND] = 0x302; // SRCALPHA
    rs[RS_DESTBLEND] = 0x303; // INVSRCALPHA
    Resource* texture = f.d.CreateTexture2(2, 2, 1, 0, 0x06); // A8R8G8B8
    WriteSolid(f.d, texture, 0, 0);
    f.d.SetTexture(0, texture);
    f.Clear(0xffff0000);
    f.Rect(0xffffffff);
    Pixel(16, 16, 0xffff0000, "an explicitly written zero texture has transparent alpha");
    rs[RS_ALPHABLENDENABLE] = 0;
    f.Rect(0xffffffff);
    Pixel(16, 16, 0xff000000, "an explicitly written zero texture has black RGB");
    WriteSolid(f.d, texture, 0, 0xff0000ff);
    f.Rect(0xffffffff);
    Pixel(16, 16, 0xff0000ff, "a texture update uploads its nonzero texels");
    WriteSolid(f.d, texture, 0, 0);
    f.Clear(0xffff0000);
    rs[RS_ALPHABLENDENABLE] = 1;
    f.Rect(0xffffffff);
    Pixel(16, 16, 0xffff0000, "updating a cached texture to zero restores transparent alpha");
    rs[RS_ALPHABLENDENABLE] = 0;
    f.Rect(0xffffffff);
    Pixel(16, 16, 0xff000000, "updating a cached texture to zero clears its old RGB");
    f.d.SetTexture(0, nullptr);
    f.d.Release(texture);
    NoErrors("zero texture uploads and alpha");

    texture = f.d.CreateTexture2(4, 4, 3, 0, 0x06);
    WriteSolid(f.d, texture, 0, 0xffff0000);
    WriteSolid(f.d, texture, 1, 0xff00ff00);
    WriteSolid(f.d, texture, 2, 0xff0000ff);
    f.d.SetTexture(0, texture);
    ts[TSS_MIPFILTER] = 0; // NONE selects MAXMIPLEVEL rather than interpolating levels.
    ts[TSS_MAXMIPLEVEL] = 1;
    f.Rect(0xffffffff);
    Pixel(16, 16, 0xff00ff00, "MIPFILTER NONE honors MAXMIPLEVEL 1 during magnification");
    ts[TSS_MAXMIPLEVEL] = 2;
    f.Rect(0xffffffff);
    Pixel(16, 16, 0xff0000ff, "MIPFILTER NONE honors a changed MAXMIPLEVEL 2");
    NoErrors("fixed mip selection");

    ts[TSS_MAXMIPLEVEL] = 0;
    ts[TSS_MIPFILTER] = 1; // POINT
    ts[TSS_ADDRESSU] = ts[TSS_ADDRESSV] = 1; // WRAP
    // 4 texels * 8 UV repeats / 32 output pixels = one texel per pixel (LOD 0).
    // A bias of +1 must therefore select the green second level.
    f.Rect(0xffffffff, .5f, 0, 32, 0, 0, 8, 8);
    Pixel(16, 16, 0xffff0000, "one source texel per output pixel selects mip 0 without bias");
    const float bias = 1;
    std::memcpy(&ts[TSS_MIPMAPLODBIAS], &bias, sizeof(bias));
    f.Rect(0xffffffff, .5f, 0, 32, 0, 0, 8, 8);
    Pixel(16, 16, 0xff00ff00, "a positive mip LOD bias selects the next smaller level");
    NoErrors("mip LOD bias");

    if (HasExtension("GL_EXT_texture_border_clamp") || HasExtension("GL_OES_texture_border_clamp")) {
        ts[TSS_MIPMAPLODBIAS] = 0;
        ts[TSS_MIPFILTER] = 0;
        ts[TSS_MAXMIPLEVEL] = 2;
        ts[TSS_ADDRESSU] = ts[TSS_ADDRESSV] = 4; // BORDER
        ts[TSS_BORDERCOLOR] = 0xff00ff00;
        f.Rect(0xffffffff, .5f, 0, 32, 2, .5f, 2, .5f);
        Pixel(16, 16, 0xff00ff00, "out-of-range UV uses the configured green border color");
        f.Rect(0xffffffff, .5f, 0, 32, .5f, .5f, .5f, .5f);
        Pixel(16, 16, 0xff0000ff, "border addressing preserves in-range blue texels");
        NoErrors("native texture border color");
    } else {
        std::printf("SKIP: native texture border color (extension unavailable)\n");
    }
    f.d.SetTexture(0, nullptr);
    f.d.Release(texture);
}
} // namespace

int main(int argc, char** argv) {
    if (!InitFormatTable()) return 1;
    // Keep independent scale coverage and explicitly exercise EGL reinitialization.
    if(argc==2&&std::strcmp(argv[1],"--scaled")==0) {
        ScaledBackbuffer();
        std::printf("Android scaled raster checks: %s (%d failures)\n",failures?"FAILED":"passed",failures);
        return failures?1:0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--reinit") == 0) ScaledBackbuffer();
    Fixture fixture;
    if (!fixture.Init()) return 1;
    Depth(fixture);
    Alpha(fixture);
    Culling(fixture);
    FlatStrip(fixture);
    PerspectiveOutlineStrip(fixture);
    RenderTexture(fixture);
    TextureSampling(fixture);
    std::printf("Android raster checks: %s (%d failures)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}
