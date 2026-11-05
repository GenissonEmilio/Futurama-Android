// NiPixelData decoding; see TextureDecode.h.
#include "render/TextureDecode.h"

#include "engine/ni/NiObjects.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

namespace fut::render {
namespace {

// Expands the masked channel of `pixel` to 8 bits (0xFF when the mask is empty).
u8 Channel(u32 pixel, u32 mask, u8 empty) {
    if (!mask) return empty;
    const int shift = std::countr_zero(mask);
    const u32 v = (pixel & mask) >> shift;
    const u32 max = mask >> shift;
    return static_cast<u8>((uint64_t(v) * 255 + max / 2) / max);
}


}  // namespace

bool DecodePixelData(const ni::NiPixelData& pd, DecodedTexture& out, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    out = DecodedTexture();
    if (pd.mips.empty()) return fail("no mip levels");
    out.width = pd.mips[0].width;
    out.height = pd.mips[0].height;
    const u32 fmt = pd.format.format;
    if (fmt == 4 || fmt == 5 || fmt == 6) {
        out.kind = fmt == 4 ? DecodedTexture::Kind::BC1 : fmt == 5 ? DecodedTexture::Kind::BC2 : DecodedTexture::Kind::BC3;
        const u32 blockBytes = fmt == 4 ? 8 : 16;
        for (const auto& m : pd.mips) {
            const u32 bw = (m.width + 3) / 4, bh = (m.height + 3) / 4;
            const size_t bytes = static_cast<size_t>(bw) * bh * blockBytes;
            if (m.offset + bytes > pd.pixels.size()) break;
            out.levels.emplace_back(pd.pixels.begin() + m.offset, pd.pixels.begin() + m.offset + bytes);
        }
        return !out.levels.empty() || fail("truncated DXT data");
    }
    if (fmt != 0 && fmt != 1) return fail("unsupported pixel format " + std::to_string(fmt));
    const u32 bpp = pd.format.bitsPerPixel;
    if (bpp != 16 && bpp != 24 && bpp != 32) return fail("unsupported bits per pixel " + std::to_string(bpp));
    const u32 bytesPer = bpp / 8;
    const u32 stride = pd.bytesPerPixel ? pd.bytesPerPixel : bytesPer;
    if (stride < bytesPer || stride > 4) return fail("invalid bytes per pixel");
    const u32* masks = pd.format.channelMasks;
    u32 occupied = 0;
    for (u32 c = 0; c < 4; ++c) {
        const u32 mask = masks[c];
        if (!mask) continue;
        const u32 shifted = mask >> std::countr_zero(mask);
        if ((bpp < 32 && (mask >> bpp)) || (shifted & (shifted + 1u)) ||
            (occupied & mask))
            return fail("invalid channel masks");
        occupied |= mask;
    }
    out.kind = DecodedTexture::Kind::RGBA8;
    u32 w = out.width, h = out.height;
    for (size_t i = 0; i < pd.mips.size(); ++i, w = std::max(1u, w / 2), h = std::max(1u, h / 2)) {
        const auto& m = pd.mips[i];
        if (!m.width || !m.height || m.width != w || m.height != h ||
            size_t(m.width) > std::numeric_limits<size_t>::max() / m.height)
            return fail("invalid mip dimensions");
        const size_t count = static_cast<size_t>(m.width) * m.height;
        const size_t end = i + 1 < pd.mips.size() ? pd.mips[i + 1].offset : pd.pixels.size();
        if (end > pd.pixels.size() || m.offset > end || count > (end - m.offset) / stride ||
            count > std::numeric_limits<size_t>::max() / 4)
            return fail("truncated pixel data");
        std::vector<u8> rgba(count * 4);
        for (u32 y = 0; y < m.height; ++y)
            for (u32 x = 0; x < m.width; ++x) {
                const size_t src = static_cast<size_t>(y) * m.width + x;
                u32 pixel = 0;
                std::memcpy(&pixel, &pd.pixels[m.offset + src * stride], bytesPer);
                u8* d = &rgba[(static_cast<size_t>(y) * m.width + x) * 4];
                d[0] = Channel(pixel, masks[0], 0);
                d[1] = Channel(pixel, masks[1], 0);
                d[2] = Channel(pixel, masks[2], 0);
                d[3] = Channel(pixel, masks[3], 0xFF);
            }
        out.levels.push_back(std::move(rgba));
    }
    return !out.levels.empty() || fail("truncated pixel data");
}

}  // namespace fut::render
