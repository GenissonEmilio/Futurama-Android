#include "core/XbeImage.h"
#include "render/TextureDecode.h"
#include "render/nixbox/NiXBoxTexturePipeline.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace {
using namespace fut;
using namespace fut::render;
using namespace fut::render::xd3d;
int failures = 0;
void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

// Entirely synthetic header and format table: no disc image, executable code or external assets are needed.
void InitFormatTable() {
    std::vector<u8> bytes(0x440);
    std::memcpy(bytes.data(), "XBEH", 4);
    auto put = [&](size_t at, u32 value) { std::memcpy(bytes.data() + at, &value, 4); };
    put(0x104, 0x10000); put(0x114, 0x3F03E9A5); put(0x118, 0x10200);
    put(0x11C, 1); put(0x120, 0x10300); put(0x208, 0x56550008); put(0x2A0, 1);
    put(0x304, 0x158864); put(0x308, 0x40); put(0x30C, 0x400); put(0x310, 0x40);
    for (u32 f = 2; f <= 5; ++f) bytes[0x400 + f] = 1;
    bytes[0x406] = bytes[0x407] = 2;
    Check(xbe::LoadGameImage(std::move(bytes)), "synthetic format table loads");
}

ni::NiPixelData* Pixels(u32 format, u32 bits, std::array<u32, 4> masks, std::vector<u8> bytes) {
    auto* p = new ni::NiPixelData;
    p->format.format = format; p->format.bitsPerPixel = bits; p->bytesPerPixel = bits / 8;
    std::copy(masks.begin(), masks.end(), p->format.channelMasks);
    p->mips.push_back({2, 2, 0}); p->pixels = std::move(bytes);
    return p;
}

void Verify(ni::NiPixelData* pixels, u32 expectedFormat, const std::vector<u8>& packed,
            const std::vector<u8>& rgba, const char* message) {
    ni::NiSourceTexture source;
    source.pixelDataObj = pixels;
    XD3DDevice device;
    nixbox::NiXBoxTextureManager manager(device);
    DecodedTexture decoded;
    Check(DecodePixelData(*pixels, decoded) && decoded.levels.size() == 1 && decoded.levels[0] == rgba, message);
    Check(manager.Precache(source), "precache succeeds");
    Resource* r = nullptr; bool set = false, mips = false;
    Check(manager.Prepare(&source, r, set, mips) && r && set && !mips, "prepared converted texture exists");
    // The four 2x2 Morton addresses equal their row-major indices, so exact packed bytes can be checked here.
    Check(r && r->format == expectedFormat && r->data == packed, message);
}

void ColorsAndAlpha() {
    const std::vector<u8> colors = {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
    const std::vector<u8> argb = {0,0,255,255, 0,255,0,255, 255,0,0,255, 255,255,255,255};
    Verify(Pixels(0,24,{0xFF0000,0xFF00,0xFF,0},
                  {0,0,255, 0,255,0, 255,0,0, 255,255,255}), 6, argb, colors,
           "RGB24 has correct pixel stride, colors and opaque alpha");
    Verify(Pixels(0,16,{0xF800,0x7E0,0x1F,0}, {0,248,224,7,31,0,255,255}),
           5, {0,248,224,7,31,0,255,255}, colors, "canonical RGB565 remains byte-identical");
    Verify(Pixels(0,32,{0xFF0000,0xFF00,0xFF,0}, argb), 7, argb, colors,
           "canonical XRGB32 remains byte-identical");
    const std::vector<u8> alpha = {255,0,0,0, 0,255,0,85, 0,0,255,136, 255,255,255,255};
    const std::vector<u8> packed = {0,0,255,0, 0,255,0,85, 255,0,0,136, 255,255,255,255};
    Verify(Pixels(1,32,{0xFF,0xFF00,0xFF0000,0xFF000000u}, alpha), 6, packed, alpha,
           "RGBA32 swapped red/blue masks and alpha survive conversion");
    Verify(Pixels(1,32,{0xFF0000,0xFF00,0xFF,0xFF000000u}, packed), 6, packed, alpha,
           "canonical ARGB32 remains byte-identical");
    Verify(Pixels(1,16,{0xF,0xF0,0xF00,0xF000}, {15,0,240,80,0,143,255,255}), 6, packed, alpha,
           "RGBA4444 noncanonical masks and intermediate alpha survive conversion");
    Verify(Pixels(1,16,{0xF00,0xF0,0xF,0xF000}, {0,15,240,80,15,128,255,255}),
           4, {0,15,240,80,15,128,255,255}, alpha, "canonical ARGB4444 remains byte-identical");
    auto* padded = Pixels(0,24,{0xFF,0xFF00,0xFF0000,0},
        {255,0,0,99, 0,255,0,99, 0,0,255,99, 255,255,255,99});
    padded->bytesPerPixel = 4;
    Verify(padded, 6, argb, colors, "RGB24 honors explicit padded pixel stride");
}

void InvalidData() {
    ni::NiSourceTexture source;
    source.pixelDataObj = Pixels(0,24,{0xFF,0xFF00,0xFF0000,0}, std::vector<u8>(11));
    DecodedTexture decoded;
    Check(!DecodePixelData(*source.pixelDataObj, decoded), "decoder rejects truncated RGB24");
    XD3DDevice device;
    nixbox::NiXBoxTextureManager manager(device);
    Check(!manager.Precache(source), "precache rejects truncated RGB24 before allocation");
    source.pixelDataObj = Pixels(1,32,{0xFF,0xFF,0xFF0000,0xFF000000u}, std::vector<u8>(16));
    Check(!DecodePixelData(*source.pixelDataObj, decoded), "decoder rejects overlapping masks");
    Check(!manager.Precache(source), "precache rejects overlapping masks");
    source.pixelDataObj = Pixels(0,16,{0xF800,0x7E0,0x1F,0}, std::vector<u8>(7));
    Check(!manager.Precache(source), "canonical fast path rejects truncation");
    source.pixelDataObj->pixels.resize(10);
    source.pixelDataObj->mips.push_back({1,1,7});
    Check(!manager.Precache(source), "mip cannot borrow bytes from the following mip");
    Check(!DecodePixelData(*source.pixelDataObj, decoded), "decoder rejects overlapping mip byte ranges");
    source.pixelDataObj = Pixels(0,24,{0xFF,0xFF00,0xFF0000,0}, std::vector<u8>(12));
    source.pixelDataObj->mips.push_back({1,1,12});
    Check(!DecodePixelData(*source.pixelDataObj, decoded), "decoder rejects a truncated trailing mip");
}

void MipsAndBC() {
    ni::NiSourceTexture source;
    source.pixelDataObj = Pixels(0,24,{0xFF,0xFF00,0xFF0000,0},
        {255,0,0, 0,255,0, 0,0,255, 255,255,255, 17,34,51});
    source.pixelDataObj->mips.push_back({1,1,12});
    XD3DDevice device;
    nixbox::NiXBoxTextureManager manager(device);
    Check(manager.Precache(source), "RGB24 mip chain converts");
    Resource* r = nullptr; bool set = false, mips = false;
    Check(manager.Prepare(&source, r, set, mips) && mips && r && r->levels == 2,
          "converted texture preserves all mip levels");
    const std::vector<u8> expected = {0,0,255,255, 0,255,0,255, 255,0,0,255, 255,255,255,255, 51,34,17,255};
    Check(r && r->data == expected, "converted trailing mip uses its own source offset");
    manager.Purge(source);
    for (u32 format = 4; format <= 6; ++format) {
        const u32 blockBytes = format == 4 ? 8 : 16;
        std::vector<u8> blocks(blockBytes * 2);
        for (size_t i = 0; i < blocks.size(); ++i) blocks[i] = u8(i * 7);
        source.pixelDataObj = Pixels(format, format == 4 ? 4 : 8, {}, blocks);
        source.pixelDataObj->mips.push_back({1,1,blockBytes});
        Check(manager.Precache(source), "BC texture precaches unchanged");
        Check(manager.Prepare(&source, r, set, mips) && r && r->data == blocks &&
              r->format == (format == 4 ? 0xC : format == 5 ? 0xE : 0xF),
              "BC1/BC2/BC3 blocks and final small mip remain byte-identical");
        DecodedTexture decoded;
        Check(DecodePixelData(*source.pixelDataObj, decoded) && decoded.levels.size() == 2 &&
              decoded.levels[1].size() == blockBytes, "BC decoder preserves a full final block");
        manager.Purge(source);
    }
}
} // namespace

int main() {
    InitFormatTable(); ColorsAndAlpha(); InvalidData(); MipsAndBC();
    std::printf("Texture conversion checks: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
