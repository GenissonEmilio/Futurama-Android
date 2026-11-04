// Android VFS, movie renderer and XMV platform services.
#pragma once

#include "core/fs/Vfs.h"
#include "game/app/MoviePlayer.h"
#include "video/xmv/XmvDecoder.h"
#include "video/xmv/XmvImage.h"

#include <vector>

namespace fut::sound {
class SoundBackend;
}

namespace fut::platform {

class MovieService final : public game::app::MovieService, public xmv::XmvPlatform {
  public:
    // `backend` null: no audio streams (a movie's audio track is not enabled: the
    // clock runs on the video alone).
    MovieService(const fs::Vfs& vfs, sound::SoundBackend* backend, const std::vector<u8>& xbe);

    render::xd3d::XD3DDevice* Device(ni::NiRenderer* renderer) override;
    std::unique_ptr<game::app::MovieFile> Open(const std::string& path) override;
    xmv::XmvPlatform* Platform() override { return this; }
    const xmv::XmvImage* Image() override { return image_.Loaded() ? &image_ : nullptr; }

    // Called by the render owner alongside AndroidSoundBackend::SetPaused.
    void SetPaused(bool paused);
    uint64_t QueryPerformanceCounter() override;
    uint64_t QueryPerformanceFrequency() override { return qpf_; }
    uint32_t DisplayRefreshRate() override { return 60; }
    void GetDisplayFieldStatus(uint32_t& field, uint32_t& vblankCount) override;
    std::unique_ptr<xmv::XmvAudioStream> CreateStream(const xmv::XmvStreamDesc& desc) override;
    int32_t SynchPlayback() override;
    void DoWork() override;
    void Sleep(uint32_t ms) override;

  private:
    const fs::Vfs& vfs_;
    sound::SoundBackend* backend_;
    xmv::XmvImage image_;
    uint64_t qpf_ = 1000000000, base_ = 0;
    uint64_t pausedAt_ = 0, pausedTicks_ = 0;
    bool paused_ = false;
};

} // namespace fut::platform
