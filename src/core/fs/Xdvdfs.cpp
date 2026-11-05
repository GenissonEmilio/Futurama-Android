// XDVDFS reader; see Xdvdfs.h.
#include "core/fs/Xdvdfs.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace fut::fs {
namespace {
constexpr u32 kSector = 2048;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
constexpr u64 kPartitionOffsets[] = {0x0, 0x18300000, 0xFD90000, 0x2080000};   // XISO, XGD1, XGD2, XGD3
constexpr u8 kAttrDirectory = 0x10;
// Links are 16-bit dword offsets; padding beyond the furthest possible entry need never be loaded.
constexpr u32 kMaxDirectoryBytes = 0xFFFFu * 4 + 14 + 255;
constexpr u64 kMaxIndexBytes = 64u << 20;
constexpr size_t kMaxEntries = 1u << 20;
constexpr size_t kMaxPathBytes = 4096;

bool Seek(std::FILE* f, u64 offset) {
    if (offset > static_cast<u64>(INT64_MAX)) return false;
#if defined(_WIN32)
    return _fseeki64(f, static_cast<s64>(offset), SEEK_SET) == 0;
#else
    static_assert(sizeof(off_t) >= sizeof(s64), "Xbox disc images need 64-bit file offsets");
    return fseeko(f, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

bool FileSize(std::FILE* f, u64& size) {
#if defined(_WIN32)
    if (_fseeki64(f, 0, SEEK_END) != 0) return false;
    const s64 end = _ftelli64(f);
#else
    if (fseeko(f, 0, SEEK_END) != 0) return false;
    const auto end = ftello(f);
#endif
    if (end < 0) return false;
    size = static_cast<u64>(end);
    return true;
}
}  // namespace

std::string Xdvdfs::Normalize(const std::string& path) {
    std::string s;
    s.reserve(path.size());
    for (char c : path) {
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        if (c == '/' && s.empty()) continue;
        s += c;
    }
    return s;
}

bool Xdvdfs::Open(const std::string& imagePath, std::string& error) {
    entries_.clear();
    activeDirectories_.clear();
    file_.reset();
    partition_ = fileSize_ = indexBytes_ = 0;
    error.clear();
    std::FILE* raw = std::fopen(imagePath.c_str(), "rb");
    auto fail = [&](const std::string& message) {
        error = message;
        entries_.clear();
        activeDirectories_.clear();
        file_.reset();
        fileSize_ = 0;
        return false;
    };
    if (!raw) return fail("cannot open " + imagePath);
    file_.reset(raw);
    if (!FileSize(raw, fileSize_)) return fail("cannot measure disc image");
    char magic[sizeof kMagic - 1];
    bool found = false;
    for (u64 off : kPartitionOffsets) {
        if (!Read(off + 32 * kSector, magic, sizeof magic)) continue;
        if (std::memcmp(magic, kMagic, sizeof magic) == 0) {
            partition_ = off;
            found = true;
            break;
        }
    }
    if (!found) return fail("not an Xbox disc image (XDVDFS volume descriptor not found)");
    u32 root[2];
    if (!Read(partition_ + 32 * kSector + sizeof magic, root, sizeof root))
        return fail("truncated volume descriptor");
    if (!Walk(root[0], root[1], "", 0)) return fail("malformed or oversized XDVDFS directory");
    return true;
}

bool Xdvdfs::Walk(u32 sector, u32 size, const std::string& prefix, int depth) {
    if (size == 0) return true;
    const u64 tableOffset = partition_ + static_cast<u64>(sector) * kSector;
    const u32 loadedSize = std::min(size, kMaxDirectoryBytes);
    if (depth > 64 || indexBytes_ > kMaxIndexBytes - loadedSize ||
        tableOffset > fileSize_ || size > fileSize_ - tableOffset) return false;
    if (!activeDirectories_.insert(tableOffset).second) return false;
    struct ActiveDirectory {
        std::set<u64>& active;
        u64 offset;
        ~ActiveDirectory() { active.erase(offset); }
    } active{activeDirectories_, tableOffset};
    indexBytes_ += loadedSize;
    std::vector<u8> table(loadedSize);
    if (!Read(tableOffset, table.data(), loadedSize)) return false;
    // The directory is a binary tree of entries at 4-byte offsets: left / right subtree links (in dwords).
    std::vector<u32> stack{0};
    std::vector<bool> seen(loadedSize / 4 + 1, false);
    while (!stack.empty()) {
        const u32 off = stack.back();
        stack.pop_back();
        if (off > loadedSize || loadedSize - off < 14 || seen[off / 4]) return false;
        seen[off / 4] = true;
        u16 left, right;
        u32 entrySector, entrySize;
        std::memcpy(&left, &table[off], 2);
        std::memcpy(&right, &table[off + 2], 2);
        std::memcpy(&entrySector, &table[off + 4], 4);
        std::memcpy(&entrySize, &table[off + 8], 4);
        const u8 attrs = table[off + 12], nameLen = table[off + 13];
        if (left == 0xFFFF && right == 0xFFFF) continue;   // padding
        if (nameLen == 0 || nameLen > loadedSize - off - 14) return false;
        const std::string name(reinterpret_cast<const char*>(&table[off + 14]), nameLen);
        if (name == "." || name == ".." || name.find_first_of("/\\") != std::string::npos ||
            name.find('\0') != std::string::npos) return false;
        if (prefix.size() + name.size() + 1 > kMaxPathBytes || entries_.size() >= kMaxEntries) return false;
        const std::string path = Normalize(prefix.empty() ? name : prefix + "/" + name);
        Entry e;
        e.offset = partition_ + static_cast<u64>(entrySector) * kSector;
        e.size = entrySize;
        e.directory = (attrs & kAttrDirectory) != 0;
        if (e.offset > fileSize_ || e.size > fileSize_ - e.offset) return false;
        if (!entries_.emplace(path, e).second) return false;
        if (e.directory && !Walk(entrySector, entrySize, path, depth + 1)) return false;
        if (left) stack.push_back(static_cast<u32>(left) * 4);
        if (right) stack.push_back(static_cast<u32>(right) * 4);
    }
    return true;
}

const Xdvdfs::Entry* Xdvdfs::Find(const std::string& path) const {
    const auto it = entries_.find(Normalize(path));
    return it == entries_.end() ? nullptr : &it->second;
}

bool Xdvdfs::Read(u64 offset, void* out, size_t size) const {
    if (!file_ || offset > fileSize_ || size > fileSize_ - offset || (size && !out)) return false;
    if (size == 0) return true;
    return Seek(file_.get(), offset) && std::fread(out, 1, size, file_.get()) == size;
}

bool Xdvdfs::ReadFile(const std::string& path, std::vector<u8>& out) const {
    const Entry* e = Find(path);
    if (!e || e->directory || e->size > kMaxReadFileBytes) return false;
    std::vector<u8> candidate(e->size);
    if (!Read(e->offset, candidate.data(), candidate.size())) return false;
    out = std::move(candidate);
    return true;
}

}  // namespace fut::fs
