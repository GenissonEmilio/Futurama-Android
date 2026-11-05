// Decompiled from default.xbe 0x0013FCD0 (ImgArchive::Find), 0x0013FEA0 (FindInArchives), 0x0013FF00 (MountArchive).
#include "core/fs/ImgArchive.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace fut::fs {
namespace {

// orig: _stricmp (0x001684D1) in the "C" locale: ASCII case folding only.
bool NameEquals(const u8* name, size_t length, const char* path) {
    if (std::strlen(path) != length) return false;
    for (size_t i = 0; i < length; ++i) {
        u8 a = name[i], b = static_cast<u8>(path[i]);
        // The first directory character carries a type bit. Fold its ASCII character as well.
        const u8 typeA = i == 0 ? a & 0x80 : 0;
        const u8 typeB = i == 0 ? b & 0x80 : 0;
        if (typeA != typeB) return false;
        if (typeA) { a &= 0x7F; b &= 0x7F; }
        if (a >= 'A' && a <= 'Z') a += 0x20;
        if (b >= 'A' && b <= 'Z') b += 0x20;
        if (a != b) return false;
    }
    return true;
}

}  // namespace

ImgArchive::ImgArchive(std::vector<u8> table) {
    if (table.size() < 4 || table.size() >= kMaxTableBytes) return;
    loadedBytes_ = table.size();
    table_ = std::move(table);
    table_.resize(loadedBytes_ + kGuardBytes, 0);
}

bool ImgArchive::ReadTable(const std::string& imgPath, std::vector<u8>& table) {
    std::FILE* f = std::fopen(imgPath.c_str(), "rb");
    if (!f) return false;
    u32 size = 0;
    bool ok = std::fread(&size, 4, 1, f) == 1 && size >= 4 && size < kMaxTableBytes;
    // A tiny truncated file must not make us allocate its untrusted declared table size.
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(imgPath, ec);
    ok = ok && !ec && fileSize >= size;
    if (ok) {
        std::vector<u8> candidate(size);
        ok = std::fseek(f, 0, SEEK_SET) == 0 && std::fread(candidate.data(), 1, size, f) == size;
        if (ok) table = std::move(candidate);
    }
    std::fclose(f);
    return ok;
}

// orig: 0x0013FCD0 ImgArchive::Find
bool ImgArchive::Find(const char* path, u32& offset, u32& size) const {
    if (!path) return false;
    // orig: 256-byte stack path, _strlwr, then '/' to '\\'.
    size_t len = 0;
    while (len < 256 && path[len]) ++len;
    if (len == 256 || loadedBytes_ < 4) return false;
    char buf[256];
    std::memcpy(buf, path, len + 1);
    for (char* c = buf; *c; ++c) {
        if (*c >= 'A' && *c <= 'Z') *c += 0x20;
        if (*c == '/') *c = '\\';
    }
    const u8* base = table_.data();
    u32 tableSize;
    std::memcpy(&tableSize, base, 4);
    if (tableSize < 4 || tableSize > loadedBytes_) return false;
    size_t e = 4, end = tableSize;
    // Directories include a final four-byte trailer in their length. The original's child scan excludes it.
    size_t extentEnd = tableSize;
    struct Parsed { size_t length, fields, next, childrenEnd; bool directory; };
    auto parse = [&](Parsed& entry, bool emptyLookup) {
        if (e >= end) return false;
        const void* nul = std::memchr(base + e, 0, end - e);
        if (!nul) return false;                 // Bound the name before any comparison or field access.
        entry.length = static_cast<const u8*>(nul) - (base + e);
        entry.fields = e + ((entry.length + 4) & ~size_t{3});
        entry.directory = (base[e] & 0x80) != 0;
        if (entry.fields > extentEnd) return false;
        if (entry.directory) {
            if (extentEnd - entry.fields < 4) return false;
            s32 length;
            std::memcpy(&length, base + entry.fields, 4);
            if (length < 4 || static_cast<size_t>(length) > extentEnd - entry.fields) return false;
            entry.next = entry.fields + static_cast<size_t>(length);
            entry.childrenEnd = entry.next - 4;
        } else {
            // Preserve the original empty-name match in zero padding, using only our deterministic guard.
            const bool guardedEmpty = emptyLookup && entry.length == 0 && extentEnd == tableSize;
            const size_t limit = guardedEmpty ? static_cast<size_t>(tableSize) + kGuardBytes : extentEnd;
            if (entry.fields > limit || limit - entry.fields < 8) return false;
            entry.next = entry.fields + 8;
        }
        return entry.next > e;
    };
    char* p = buf;
    for (;;) {
        char* q = std::strchr(p, '\\');
        if (q) {
            *q = 0;
            *p = static_cast<char>(static_cast<u8>(*p) | 0x80);
        }
        Parsed entry{};
        for (;;) {
            if (!parse(entry, !q && *p == 0)) return false;
            if (NameEquals(base + e, entry.length, p)) break;
            e = entry.next;
        }
        if (q) {
            if (!entry.directory) return false;
            extentEnd = entry.next;
            end = entry.childrenEnd;
            e = entry.fields + 4;
            p = q + 1;
        } else {
            if (entry.directory) return false;
            std::memcpy(&size, base + entry.fields, 4);
            std::memcpy(&offset, base + entry.fields + 4, 4);
            return true;
        }
    }
}

void ArchiveSet::Add(std::string imgPath, ImgArchive archive) {
    paths_.push_back(std::move(imgPath));
    archives_.push_back(std::move(archive));
}

// orig: 0x0013FEA0 FindInArchives (first match in mount order)
bool ArchiveSet::Find(const char* name, Hit& hit) const {
    for (size_t i = 0; i < archives_.size(); ++i) {
        if (archives_[i].Find(name, hit.offset, hit.size)) {
            hit.archive = i;
            return true;
        }
    }
    return false;
}

}  // namespace fut::fs
