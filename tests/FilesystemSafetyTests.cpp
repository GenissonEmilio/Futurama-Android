#include "core/fs/ImgArchive.h"
#include "core/fs/Vfs.h"
#include "core/fs/Xdvdfs.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {
using namespace fut;
int failures = 0;
void Check(bool value, const char* what) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}
void Put16(std::vector<u8>& data, size_t at, u16 value) {
    data[at] = static_cast<u8>(value);
    data[at + 1] = static_cast<u8>(value >> 8);
}
void Put32(std::vector<u8>& data, size_t at, u32 value) {
    for (unsigned i = 0; i < 4; ++i) data[at + i] = static_cast<u8>(value >> (i * 8));
}
struct Scratch {
    std::filesystem::path root;
    Scratch() {
        root = std::filesystem::temp_directory_path() /
            ("futurama-fs-safety-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(root);
    }
    ~Scratch() { std::error_code ec; std::filesystem::remove_all(root, ec); }
    std::string Write(const std::string& name, const std::vector<u8>& data) const {
        const auto path = root / name;
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        out.close();
        Check(static_cast<bool>(out), "write synthetic fixture");
        return path.string();
    }
};

std::vector<u8> Archive() {
    std::vector<u8> data(64);
    Put32(data, 0, static_cast<u32>(data.size()));
    data[4] = 'D' | 0x80; data[5] = 'i'; data[6] = 'r';
    Put32(data, 8, 28);       // directory children [12, 32), sibling at 36
    data[12] = 'A'; Put32(data, 16, 3); Put32(data, 20, 64);
    data[36] = 'B'; Put32(data, 40, 2); Put32(data, 44, 67);
    return data;
}
void TestImg(const Scratch& scratch) {
    u32 offset = 0, size = 0;
    const auto valid = Archive();
    fs::ImgArchive archive(valid);
    Check(archive.Find("dIR/a", offset, size) && offset == 64 && size == 3, "IMG nested case-insensitive lookup");
    Check(archive.Find("B", offset, size) && offset == 67 && size == 2, "IMG skip valid directory");
    Check(!archive.Find("Dir/missing", offset, size), "IMG missing child terminates");
    Check(!archive.Find(nullptr, offset, size), "IMG null path");
    Check(!archive.Find(std::string(256, 'a').c_str(), offset, size), "IMG long path");
    auto bad = valid;
    for (u32 length : {0u, 0xFFFFFFFCu, 0x7FFFFFFFu}) {
        Put32(bad, 8, length);
        Check(!fs::ImgArchive(bad).Find("B", offset, size), "IMG invalid directory skip length");
        Check(!fs::ImgArchive(bad).Find("Dir/A", offset, size), "IMG invalid directory descent length");
    }
    bad.assign(28, 0); Put32(bad, 0, 28);
    bad[4] = 'D' | 0x80; Put32(bad, 8, 4);  // empty directory followed by a file
    bad[12] = 'B'; Put32(bad, 16, 2); Put32(bad, 20, 28);
    Check(fs::ImgArchive(bad).Find("B", offset, size) && offset == 28, "IMG skip valid empty directory");
    bad.assign(12, 0xFF); Put32(bad, 0, 12);
    Check(!fs::ImgArchive(bad).Find("missing", offset, size), "IMG unterminated name excludes appended guard");
    bad.assign(12, 0); Put32(bad, 0, 12); bad[4] = 'A';
    Check(!fs::ImgArchive(bad).Find("A", offset, size), "IMG truncated file fields");
    bad = valid; Put32(bad, 0, static_cast<u32>(bad.size() + 4));
    Check(!fs::ImgArchive(bad).Find("Dir/A", offset, size), "IMG declared table cannot include guard bytes");
    bad.assign(8, 0); Put32(bad, 0, 8);
    Check(fs::ImgArchive(bad).Find("", offset, size) && offset == 0 && size == 0, "IMG original empty-padding guard behavior");
    std::vector<u8> loaded{9};
    auto path = scratch.Write("valid-table.img", valid);
    Check(fs::ImgArchive::ReadTable(path, loaded) && loaded == valid, "IMG read valid table");
    bad.assign(4, 0); Put32(bad, 0, (64u << 20) - 1);
    path = scratch.Write("truncated-table.img", bad);
    Check(!fs::ImgArchive::ReadTable(path, loaded) && loaded == valid, "IMG reject large truncated table before resize");
    Put32(bad, 0, 0xFFFFFFFFu);
    path = scratch.Write("huge-table.img", bad);
    Check(!fs::ImgArchive::ReadTable(path, loaded), "IMG table memory budget");
}

constexpr size_t kSector = 2048, kRoot = 33 * kSector;
void Node(std::vector<u8>& disc, size_t at, const std::string& name, u32 sector, u32 size, bool directory) {
    Put32(disc, at + 4, sector); Put32(disc, at + 8, size);
    disc[at + 12] = directory ? 0x10 : 0; disc[at + 13] = static_cast<u8>(name.size());
    std::memcpy(disc.data() + at + 14, name.data(), name.size());
}
std::vector<u8> Disc() {
    std::vector<u8> data(40 * kSector);
    const char magic[] = "MICROSOFT*XBOX*MEDIA";
    std::memcpy(data.data() + 32 * kSector, magic, sizeof(magic) - 1);
    Put32(data, 32 * kSector + sizeof(magic) - 1, 33);
    Put32(data, 32 * kSector + sizeof(magic) + 3, kSector);
    Node(data, kRoot, "Mixed.Bin", 35, 3, false);
    Put16(data, kRoot + 2, 8);  // right node at byte 32
    Node(data, kRoot + 32, "Folder", 34, kSector, true);
    Node(data, 34 * kSector, "Child", 36, 2, false);
    data[35 * kSector] = 1; data[35 * kSector + 1] = 2; data[35 * kSector + 2] = 3;
    data[36 * kSector] = 4; data[36 * kSector + 1] = 5;
    return data;
}
void TestXdvdfs(const Scratch& scratch) {
    const auto good = Disc();
    fs::Xdvdfs image;
    std::string error;
    auto path = scratch.Write("valid.iso", good);
    Check(image.Open(path, error) && error.empty() && image.FileCount() == 3, "XDVDFS valid tree and directory");
    std::vector<u8> out;
    Check(image.ReadFile("/MIXED.BIN", out) && out == std::vector<u8>({1, 2, 3}), "XDVDFS valid file bytes");
    Check(image.ReadFile("folder\\CHILD", out) && out == std::vector<u8>({4, 5}), "XDVDFS nested lookup");
    Check(!image.Read(std::numeric_limits<u64>::max(), out.data(), 2), "XDVDFS overflowing read offset");
    Check(!image.Read(0, nullptr, 1), "XDVDFS null read destination");
    Check(image.Read(good.size(), nullptr, 0), "XDVDFS empty read at EOF");
    auto Reject = [&](std::vector<u8> bad, const char* what) {
        const auto badPath = scratch.Write("invalid.iso", bad);
        Check(!image.Open(badPath, error) && !error.empty() && image.FileCount() == 0, what);
        Check(!image.Read(0, out.data(), 1), "XDVDFS failed Open closes previous image");
    };
    auto bad = good; bad.resize(32 * kSector + 23);
    Reject(bad, "XDVDFS truncated descriptor");
    bad = good; Put32(bad, 32 * kSector + 24, 0xFFFFFFFFu);
    Reject(bad, "XDVDFS oversized root table");
    bad = good; Put32(bad, 32 * kSector + 24, 13);
    Reject(bad, "XDVDFS truncated directory header");
    bad = good; Put32(bad, 32 * kSector + 24, 20);
    Reject(bad, "XDVDFS truncated directory name");
    bad = good; Put16(bad, kRoot + 32 + 2, 8);
    Reject(bad, "XDVDFS binary tree cycle");
    bad = good; Put32(bad, kRoot + 32 + 4, 33);
    Reject(bad, "XDVDFS recursive directory cycle");
    bad = good; Put16(bad, kRoot + 2, 0xFFFE);
    Reject(bad, "XDVDFS child link outside table");
    bad = good; Put32(bad, kRoot + 8, 0xFFFFFFFFu);
    Reject(bad, "XDVDFS file extent outside image");
    bad = good; bad[kRoot + 14] = 0;
    Reject(bad, "XDVDFS embedded NUL in name");
    // A sparse large file validates that whole-file memory limits are checked independently of extents.
    auto large = good;
    Put32(large, kRoot + 8, static_cast<u32>(fs::Xdvdfs::kMaxReadFileBytes + 1));
    path = scratch.Write("large.iso", large);
    std::error_code ec;
    std::filesystem::resize_file(path, 35 * kSector + fs::Xdvdfs::kMaxReadFileBytes + 1, ec);
    Check(!ec, "create sparse large file fixture");
    if (!ec) {
        Check(image.Open(path, error), "XDVDFS valid large file extent remains indexable");
        out = {42};
        Check(!image.ReadFile("Mixed.Bin", out) && out == std::vector<u8>({42}), "XDVDFS whole-file budget before allocation");
        u8 byte = 0;
        Check(image.Read(35 * kSector, &byte, 1) && byte == 1, "XDVDFS large file can still stream");
    }
}

void TestVfs(const Scratch& scratch) {
    auto img = Archive();
    img.insert(img.end(), {1, 2, 3, 4, 5});
    const auto archivePath = scratch.Write("sysNTSC.img", img);
    fs::Vfs vfs;
    std::string error;
    Check(vfs.Open(scratch.root.string(), error), "VFS valid folder archive mount");
    std::vector<u8> out;
    Check(vfs.ReadFile("DIR\\a", out) && out == std::vector<u8>({1, 2, 3}), "VFS valid archive file bytes");
    scratch.Write("loose.bin", {6, 7});
    Check(vfs.ReadFile("LOOSE.BIN", out) && out == std::vector<u8>({6, 7}), "VFS loose-file case folding");
    u8 byte = 0;
    Check(!vfs.ReadDisc("loose.bin", std::numeric_limits<u64>::max(), &byte, 2), "VFS overflow-safe streamed read");
    Check(!vfs.ReadDisc("loose.bin", 0, nullptr, 1), "VFS null streamed destination");
    Check(vfs.ReadDisc("loose.bin", 2, nullptr, 0), "VFS empty streamed read at EOF");
    Check(!vfs.ReadDisc("../loose.bin", 0, &byte, 1), "VFS reject parent path component");
    Check(!vfs.ReadFile(std::string("Dir/A\0suffix", 12), out), "VFS reject embedded NUL archive path");
    const auto loosePath = (scratch.root / "loose.bin").string();
    std::error_code largeError;
    std::filesystem::resize_file(loosePath, fs::Xdvdfs::kMaxReadFileBytes + 1, largeError);
    Check(!largeError, "create sparse loose file fixture");
    if (!largeError) {
        out = {42};
        Check(!vfs.ReadFile("loose.bin", out) && out == std::vector<u8>({42}), "VFS loose whole-file memory budget");
        Check(vfs.ReadDisc("loose.bin", 0, &byte, 1) && byte == 6, "VFS large loose file can still stream");
    }
    auto bad = img;
    Put32(bad, 16, 0xFFFFFFFFu);
    scratch.Write("sysNTSC.img", bad);
    Check(vfs.Open(scratch.root.string(), error), "VFS can mount bounded table with invalid file extent");
    out = {42};
    Check(!vfs.ReadFile("Dir/A", out) && out == std::vector<u8>({42}), "VFS invalid IMG file extent before allocation");
    Check(!vfs.Exists("Dir/A"), "VFS invalid IMG extent is not an existing file");
    bad.assign(4, 0); Put32(bad, 0, 0xFFFFFFFFu);
    scratch.Write("sysNTSC.img", bad);
    Check(!vfs.Open(scratch.root.string(), error), "VFS huge truncated table rejects mount");
    // Verify the table budget even when its declared extent is backed by a sparse file.
    bad.assign(4, 0); Put32(bad, 0, static_cast<u32>(fs::ImgArchive::kMaxTableBytes));
    scratch.Write("sysNTSC.img", bad);
    std::error_code ec;
    std::filesystem::resize_file(archivePath, fs::ImgArchive::kMaxTableBytes, ec);
    Check(!ec, "create sparse huge table fixture");
    if (!ec) Check(!vfs.Open(scratch.root.string(), error), "VFS IMG table memory budget before allocation");
}
}  // namespace

int main() {
    Scratch scratch;
    TestImg(scratch);
    TestXdvdfs(scratch);
    TestVfs(scratch);
    std::printf("Filesystem safety checks: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
