// The console's movie services; see MovieService.h.
#include "platform/android/MovieService.h"

#include "engine/ni/NiRenderer.h"
#include "game/sound/SoundSystem.h"

#include <chrono>
#include <thread>

#include <algorithm>

namespace fut::platform {

namespace {

uint64_t MonotonicTicks() {
    return uint64_t(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

// The player's NiUDSStream over a disc file (synchronous reads at the stream's
// position).
class DiscMovieFile final : public game::app::MovieFile {
  public:
    DiscMovieFile(const fs::Vfs& vfs, std::string path, u64 size) : vfs_(vfs), path_(std::move(path)), size_(size) {}
    u32 Read(void* dst, u32 bytes) override {
        const u64 n = std::min<u64>(bytes, pos_ < size_ ? size_ - pos_ : 0);
        if (n && !vfs_.ReadDisc(path_, pos_, dst, size_t(n)))
            return 0;
        pos_ += n;
        return u32(n);
    }
    void Seek(u64 offset) override { pos_ = offset; }

  private:
    const fs::Vfs& vfs_;
    std::string path_;
    u64 size_ = 0, pos_ = 0;
};

// IDirectSoundStream over the sound backend's stream.
class BackendStream final : public xmv::XmvAudioStream {
  public:
    explicit BackendStream(std::unique_ptr<sound::SoundStream> s) : s_(std::move(s)) {}
    int32_t Pause(uint32_t flags) override { return s_->Pause(flags); }
    int32_t Process(const xmv::XmvMediaPacket& p) override {
        return s_->Process(p.data, p.size, p.completedSize, p.status, p.context);
    }
    int32_t Discontinuity() override { return s_->Discontinuity(); }
    uint32_t GetStatus() override { return s_->GetStatus(); }
    int32_t FlushEx(uint64_t, uint32_t) override { return s_->Flush(); }
    void SetVolume(int32_t v) override { s_->SetVolume(v); }

  private:
    std::unique_ptr<sound::SoundStream> s_;
};

} // namespace

MovieService::MovieService(const fs::Vfs& vfs, sound::SoundBackend* backend, const std::vector<u8>& xbe)
    : vfs_(vfs), backend_(backend) {
    image_.LoadFromXbe(xbe);
    base_ = QueryPerformanceCounter();
}

render::xd3d::XD3DDevice* MovieService::Device(ni::NiRenderer* renderer) {
    return renderer ? renderer->D3DDevice() : nullptr;
}

std::unique_ptr<game::app::MovieFile> MovieService::Open(const std::string& path) {
    u64 size = 0;
    if (!vfs_.DiscFileSize(path, size))
        return nullptr;
    return std::make_unique<DiscMovieFile>(vfs_, path, size);
}

void MovieService::SetPaused(bool paused) {
    if (paused == paused_)
        return;
    const uint64_t now = MonotonicTicks();
    if (paused)
        pausedAt_ = now;
    else
        pausedTicks_ += now - pausedAt_;
    paused_ = paused;
}

uint64_t MovieService::QueryPerformanceCounter() { return (paused_ ? pausedAt_ : MonotonicTicks()) - pausedTicks_; }

void MovieService::GetDisplayFieldStatus(uint32_t& field, uint32_t& vblankCount) {
    // (L) 59.94 fields a second since the service started, fields alternating
    // even (2) / odd (1)
    const u64 t = QueryPerformanceCounter() - base_;
    const u64 period = 1001 * qpf_;
    vblankCount = uint32_t((t / period) * 60000 + (t % period) * 60000 / period);
    field = (vblankCount & 1) ? 1u : 2u;
}

std::unique_ptr<xmv::XmvAudioStream> MovieService::CreateStream(const xmv::XmvStreamDesc& desc) {
    if (!backend_)
        return nullptr;
    // The backend timestamps completions on its raw monotonic clock. XMV uses
    // the lifecycle-adjusted clock for both audio callbacks and video deadlines.
    std::unique_ptr<sound::SoundStream> s =
        backend_->CreateStream(desc.format, [this, callback = desc.callback](u32 context, u32 status, u64) {
            if (callback)
                callback(context, status, QueryPerformanceCounter());
        });
    if (!s)
        return nullptr;
    return std::make_unique<BackendStream>(std::move(s));
}

int32_t MovieService::SynchPlayback() { return backend_ ? backend_->SynchPlayback() : 0; }

void MovieService::DoWork() {
    // Movie packets can advance while the decoder waits between game steps.
    if (backend_)
        backend_->DoStreamWork();
}

void MovieService::Sleep(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

} // namespace fut::platform
