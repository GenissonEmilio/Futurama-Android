#include "game/app/SaveStorage.h"

#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
using namespace fut;
using namespace fut::game::app;
int failures = 0;
void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("fut-save-tests-" + std::to_string(nonce));
        std::filesystem::create_directory(path);
    }
    ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

std::vector<u8> Read(SaveStorage& storage, const std::string& path) {
    const u32 handle = storage.CreateFile(path, 0x80000000u, 3);
    Check(handle != SaveStorage::kInvalid, "open committed file");
    std::vector<u8> data;
    Check(storage.ReadFile(handle, 4096, data), "read committed file");
    Check(storage.CloseHandle(handle), "close committed file");
    return data;
}

bool Write(SaveStorage& storage, const std::string& path, const std::vector<u8>& bytes) {
    const u32 handle = storage.CreateFile(path, 0x40000000u, 2);
    Check(handle != SaveStorage::kInvalid, "open save for replacement");
    const bool ok = storage.WriteFileEx(handle, bytes.data(), static_cast<u32>(bytes.size()), 0x000E0230);
    Check(storage.CloseHandle(handle), "close replacement handle");
    return ok;
}

void Completion(SaveStorage& storage, u32 bytes) {
    SaveStorage::Completion c;
    Check(storage.TakeCompletion(c) && c.routine == 0x000E0230 && c.error == 0 && c.bytes == bytes,
          "one successful completion after durable write");
    Check(!storage.TakeCompletion(c), "completion consumed exactly once");
}

void TestSuccessAndRecovery(const std::filesystem::path& root) {
    const std::vector<u8> initial{0, 1, 2, 0xFF, 4}, replacement{9, 8};
    std::string path;
    HostSaveStorage storage(root);
    Check(storage.CreateSaveGame(u"Fry \u00E9", 1, 0, path, 0) == 0, "create host save");
    Check(Write(storage, path + "SAVE.DAT", initial), "persist initial binary payload");
    Completion(storage, static_cast<u32>(initial.size()));
    storage.readDisc = [](const std::string& source, std::vector<u8>& out) {
        if (source != "SaveGameIcons/saveimage.xbx") return false;
        out = {0x11, 0x22};
        return true;
    };
    Check(storage.CopyFile("d:\\SaveGameIcons\\saveimage.xbx", path + "saveimage.xbx"), "persist disc icon");
    HostSaveStorage loaded(root);
    Check(loaded.saves.size() == 1 && loaded.saves[0].name == u"Fry \u00E9", "reload UTF-8 save name");
    Check(Read(loaded, path + "save.dat") == initial, "reload binary file with case-insensitive lookup");
    Check(Read(loaded, path + "SAVEIMAGE.XBX") == std::vector<u8>({0x11, 0x22}), "reload copied icon");
    Check(Write(loaded, path + "SAVE.DAT", replacement), "replace existing save with shorter payload");
    Completion(loaded, static_cast<u32>(replacement.size()));
    HostSaveStorage replaced(root);
    Check(Read(replaced, path + "save.dat") == replacement, "replacement removes old trailing bytes on reload");

    const auto live = root / "000000000001";
    const auto backup = root / ".save-000000000001.backup";
    const auto pending = root / ".save-000000000001.pending";
    std::filesystem::rename(live, backup);             // interruption after moving the old save aside
    std::filesystem::create_directory(pending);
    std::ofstream(pending / "name.txt") << "uncommitted";
    std::ofstream(pending / "SAVE.DAT") << "partial";
    HostSaveStorage recovered(root);
    Check(recovered.saves.size() == 1 && Read(recovered, path + "save.dat") == replacement,
          "interrupted replacement restores backup and ignores partial staging");
    Check(!std::filesystem::exists(backup), "backup restored to the original directory");
    Check(Write(recovered, path + "SAVE.DAT", initial), "write succeeds after interrupted transaction");
    Completion(recovered, static_cast<u32>(initial.size()));
    Check(!std::filesystem::exists(pending), "successful write cleans stale staging");

    std::filesystem::copy(live, backup, std::filesystem::copy_options::recursive);
    std::ofstream(backup / "SAVE.DAT", std::ios::binary | std::ios::trunc) << "old backup";
    HostSaveStorage committed(root);                 // interruption after installing the complete replacement
    Check(committed.saves.size() == 1 && Read(committed, path + "save.dat") == initial,
          "installed replacement wins over an obsolete backup");
    Check(!std::filesystem::exists(backup), "obsolete backup removed on recovery");
    Check(committed.DeleteSaveGame(u"Fry \u00E9") == 0, "delete committed save");
    Check(HostSaveStorage(root).saves.empty(), "deleted save stays deleted on reload");
}

void TestFailures(const std::filesystem::path& root) {
    const std::vector<u8> initial{1, 2, 3}, replacement{7, 8};
    std::string path;
    HostSaveStorage storage(root);
    Check(storage.CreateSaveGame(u"Leela", 1, 0, path, 0) == 0, "create failure-test save");
    Check(Write(storage, path + "save.dat", initial), "persist failure-test baseline");
    Completion(storage, static_cast<u32>(initial.size()));

    // Fail after staging name.txt, while writing another file. The live directory must remain unchanged.
    const std::string tooLong(300, 'x');
    storage.saves[0].files[tooLong] = MemorySaveStorage::File{tooLong, {5}};
    Check(!Write(storage, path + "save.dat", replacement), "host stream failure propagates to WriteFileEx");
    SaveStorage::Completion completion;
    Check(!storage.TakeCompletion(completion), "failed write never queues a successful completion");
    Check(Read(storage, path + "save.dat") == initial, "failed CREATE_ALWAYS restores prior in-memory data");
    HostSaveStorage reloaded(root);
    Check(Read(reloaded, path + "save.dat") == initial, "failed staging preserves prior on-disk data");
    Check(!std::filesystem::exists(root / ".save-000000000001.pending"), "failed staging is cleaned");
    const u32 newFile = storage.CreateFile(path + "retry.dat", 0x40000000u, 2);
    Check(newFile != SaveStorage::kInvalid && !storage.WriteFileEx(newFile, replacement.data(), 2, 0x000E0230),
          "new file write fails while staging is blocked");
    Check(!storage.saves[0].files.count("retry.dat"), "failed new-file write restores absent file");
    storage.saves[0].files.erase(tooLong);
    Check(storage.WriteFileEx(newFile, replacement.data(), 2, 0x000E0230), "retry newly created file on the same handle");
    Completion(storage, 2);
    Check(storage.CloseHandle(newFile), "close retried new-file handle");
    Check(Read(storage, path + "retry.dat") == replacement, "new-file retry contains the requested bytes");

    // A regular file at the storage root deterministically rejects all filesystem mutations on Windows and Android.
    const auto moved = root.parent_path() / "saved-root";
    std::filesystem::rename(root, moved);
    std::ofstream(root, std::ios::binary) << "root blocker";
    Check(!Write(storage, path + "save.dat", replacement), "unavailable root reports write failure");
    Check(!storage.TakeCompletion(completion), "unavailable root queues no false success");
    Check(Read(storage, path + "save.dat") == initial, "root failure rolls back truncation");
    storage.readDisc = [](const std::string&, std::vector<u8>& out) { out = {6}; return true; };
    Check(!storage.CopyFile("d:\\icon.xbx", path + "icon.xbx"), "copy persistence failure is reported");
    Check(!storage.saves[0].files.count("icon.xbx"), "failed copy restores file map");
    Check(storage.DeleteSaveGame(u"Leela") != 0 && storage.saves.size() == 1, "failed deletion retains save model");
    std::string failedPath = "stale";
    Check(storage.CreateSaveGame(u"Bender", 1, 0, failedPath, 0) != 0 && failedPath.empty() && storage.saves.size() == 1,
          "failed save creation reports error and rolls back enumeration");
    const u32 retry = storage.CreateFile(path + "save.dat", 0x40000000u, 2);
    Check(retry != SaveStorage::kInvalid && !storage.WriteFileEx(retry, replacement.data(), 2, 0x000E0230),
          "replacement fails while retaining its handle");
    std::filesystem::remove(root);
    std::filesystem::rename(moved, root);
    HostSaveStorage restored(root);
    Check(restored.saves.size() == 1 && Read(restored, path + "save.dat") == initial,
          "all failed operations leave the original disk save reloadable");
    Check(storage.WriteFileEx(retry, replacement.data(), 2, 0x000E0230), "retry existing file on the same handle");
    Completion(storage, static_cast<u32>(replacement.size()));
    Check(storage.CloseHandle(retry), "close retried replacement handle");
    HostSaveStorage retried(root);
    Check(Read(retried, path + "save.dat") == replacement, "successful retry survives reload");
}
}  // namespace

int main() {
    try {
        TemporaryDirectory temporary;
        TestSuccessAndRecovery(temporary.path / "success");
        TestFailures(temporary.path / "failures");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: unexpected filesystem exception: %s\n", e.what());
        ++failures;
    }
    std::printf("Save storage checks: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
