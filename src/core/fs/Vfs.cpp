// VFS over the user's disc; see Vfs.h.
#include "core/fs/Vfs.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>

namespace fut::fs {
namespace {

// A disc path inside a folder source, matched case-insensitively component by component.
bool ResolveInFolder(const std::string& root, const std::string& name, std::filesystem::path& out) {
    namespace sfs = std::filesystem;
    if (root.empty() || name.find('\0') != std::string::npos) return false;
    sfs::path cur(root);
    const std::string norm = Xdvdfs::Normalize(name);
    size_t pos = 0;
    while (pos <= norm.size()) {
        const size_t next = norm.find('/', pos);
        const std::string part = norm.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        pos = next == std::string::npos ? norm.size() + 1 : next + 1;
        if (part.empty()) continue;
        if (part == "." || part == ".." || part.find(':') != std::string::npos) return false;
        std::error_code ec;
        bool found = false;
        for (sfs::directory_iterator it(cur, ec), last; !ec && it != last; it.increment(ec)) {
            const auto& entry = *it;
            if (Xdvdfs::Normalize(entry.path().filename().string()) == part) {
                cur = entry.path();
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    out = cur;
    return true;
}

}  // namespace

bool Vfs::Open(const std::string& source, std::string& error) {
    namespace sfs = std::filesystem;
    image_.reset();
    directory_.clear();
    archives_ = ArchiveSet();
    pal_ = false;
    error.clear();
    std::error_code ec;
    if (sfs::is_directory(source, ec)) {
        directory_ = source;
    } else {
        image_ = std::make_unique<Xdvdfs>();
        if (!image_->Open(source, error)) {
            image_.reset();
            return false;
        }
    }
    // orig: 0x000D2730.
    pal_ = Mount("sysPAL.img");
    if (!pal_) Mount("sysNTSC.img");
    u32 n = 0;
    char name[32];
    for (; n < 100; ++n) {
        std::snprintf(name, sizeof name, "FutPAL%02u.img", n);
        if (!Mount(name)) break;
    }
    if (n == 0) {
        for (; n < 100; ++n) {
            std::snprintf(name, sizeof name, "FutNTSC%02u.img", n);
            if (!Mount(name)) break;
        }
    } else {
        pal_ = true;
    }
    if (archives_.Count() == 0) {
        error = "no game archives (sys*.img / Fut*.img) on the disc";
        return false;
    }
    return true;
}

bool Vfs::DiscFileSize(const std::string& path, u64& size) const {
    if (image_) {
        const Xdvdfs::Entry* e = image_->Find(path);
        if (!e || e->directory) return false;
        size = e->size;
        return true;
    }
    std::filesystem::path p;
    std::error_code ec;
    if (!ResolveInFolder(directory_, path, p) || !std::filesystem::is_regular_file(p, ec)) return false;
    size = std::filesystem::file_size(p, ec);
    return !ec;
}

bool Vfs::ReadDisc(const std::string& path, u64 offset, void* out, size_t size) const {
    if (size && !out) return false;
    if (image_) {
        const Xdvdfs::Entry* e = image_->Find(path);
        if (!e || e->directory || offset > e->size || size > e->size - offset) return false;
        return image_->Read(e->offset + offset, out, size);
    }
    std::filesystem::path p;
    std::error_code ec;
    if (!ResolveInFolder(directory_, path, p) || !std::filesystem::is_regular_file(p, ec)) return false;
    const auto fileSize = std::filesystem::file_size(p, ec);
    if (ec || offset > fileSize || size > fileSize - offset ||
        offset > static_cast<u64>(std::numeric_limits<std::streamoff>::max()) ||
        size > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) return false;
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    if (size == 0) return true;
    in.seekg(static_cast<std::streamoff>(offset));
    if (!in) return false;
    in.read(static_cast<char*>(out), static_cast<std::streamsize>(size));
    return static_cast<size_t>(in.gcount()) == size;
}
bool Vfs::Mount(const std::string& imgName) {          // orig: MountArchive 0x00140040 (table read like ReadTable)
    u64 fileSize;
    if (!DiscFileSize(imgName, fileSize) || fileSize < 4) return false;
    u32 tableSize;
    if (!ReadDisc(imgName, 0, &tableSize, 4) || tableSize < 4 || tableSize >= ImgArchive::kMaxTableBytes || tableSize > fileSize) return false;
    std::vector<u8> table(tableSize);
    if (!ReadDisc(imgName, 0, table.data(), tableSize)) return false;
    archives_.Add(imgName, ImgArchive(std::move(table)));
    return true;
}

bool Vfs::ReadFile(const std::string& name, std::vector<u8>& out) const {
    if (name.find('\0') != std::string::npos) return false;
    ArchiveSet::Hit hit;
    u64 size, offset = 0;
    std::string source = name;
    if (archives_.Find(name.c_str(), hit)) {
        source = archives_.PathOf(hit.archive);
        u64 archiveSize;
        if (!DiscFileSize(source, archiveSize) || hit.offset > archiveSize || hit.size > archiveSize - hit.offset)
            return false;
        offset = hit.offset;
        size = hit.size;
    } else if (!DiscFileSize(name, size)) {
        return false;
    }
    if (size > Xdvdfs::kMaxReadFileBytes) return false;
    std::vector<u8> candidate(static_cast<size_t>(size));
    if (!ReadDisc(source, offset, candidate.data(), candidate.size())) return false;
    out = std::move(candidate);
    return true;
}

bool Vfs::Exists(const std::string& name) const {
    if (name.find('\0') != std::string::npos) return false;
    ArchiveSet::Hit hit;
    u64 size;
    if (archives_.Find(name.c_str(), hit)) {
        return DiscFileSize(archives_.PathOf(hit.archive), size) && hit.offset <= size && hit.size <= size - hit.offset;
    }
    return DiscFileSize(name, size);
}
}  // namespace fut::fs
