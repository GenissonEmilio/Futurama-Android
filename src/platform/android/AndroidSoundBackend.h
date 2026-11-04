// Android device output for the original XDK DirectSound, VP and DSP pipeline.
#pragma once
#include "audio/xdsound/XdsDirectSound.h"
#include "game/sound/SoundSystem.h"
#include <memory>
namespace fut::audio::vp {
struct Levels;
}
namespace fut::platform {
class AndroidSoundBackend final : public sound::SoundBackend {
  public:
    struct Options {
        bool stepStatus = true;
        bool surround = false;
    };
    AndroidSoundBackend();
    explicit AndroidSoundBackend(const Options& options);
    ~AndroidSoundBackend() override;
    bool Init();
    // No device: callers advance the hardware explicitly with RenderFrames.
    bool InitOffline();
    void LoadXbeTables(const std::vector<u8>& xbe);
    void SetEffectsImage(const std::vector<u8>& dsstdfx);
    const audio::xds::DirectSound& Model() const;
    // Offline only: append 32-sample frames (24-bit signed interleaved samples).
    bool RenderFrames(u32 frames, std::vector<s32>& out);
    void SetLevels(audio::vp::Levels* levels);
    bool HardwarePlaying(Buffer b);
    u32 OutputChannels() const;
    // Device output renders on its own worker; pause freezes the VP/DSP timeline.
    void SetPaused(bool paused);
    void DownloadEffectsImage() override;
    void UseFullHrtf() override {} // (the full-HRTF calculator is the one ported)
    void SetDopplerFactor(f32 f, u32 apply) override;
    void SetDistanceFactor(f32 f, u32 apply) override;
    void SetRolloffFactor(f32 f, u32 apply) override;
    void SetI3DL2Listener(u32 preset, u32 apply) override;
    void ListenerPosition(f32 x, f32 y, f32 z, u32 apply) override;
    void ListenerVelocity(f32 x, f32 y, f32 z, u32 apply) override;
    void ListenerOrientation(f32 fx, f32 fy, f32 fz, f32 tx, f32 ty, f32 tz, u32 apply) override;
    void CommitDeferredSettings() override;
    void DoWork() override;
    Buffer CreateBuffer(u32 flags, const std::vector<u8>& format) override;
    void SetBufferData(Buffer b, const u8* data, u32 bytes) override;
    void Play(Buffer b, u32 flags) override;
    void Stop(Buffer b) override;
    // The library's CMcpxStream (audio/vp/VpStream.h): the
    // packets as SSL segments of the stream voice, refilled after the VP's
    // notifier interrupt (serviced at DoWork), completed by DoWork's deferred
    // command (docs/analysis/audio_port.md 5.8).
    Buffer CreateStreamVoice(u32 flags, u32 maxPackets, const std::vector<u8>& format) override;
    void ProcessPacket(Buffer s, const u8* data, u32 size, u32* status) override;
    void PauseStream(Buffer b, bool pause) override;
    u32 GetStatus(Buffer b) override;
    void Release(Buffer b) override;
    void SetVolume(Buffer b, s32 volume) override;
    void SetFrequency(Buffer b, u32 hz) override;
    void SetMixBins(Buffer b, const sound::MixBin* bins, u32 count) override;
    void SetMinDistance(Buffer b, f32 d, u32 apply) override;
    void SetMaxDistance(Buffer b, f32 d, u32 apply) override;
    void SetPosition(Buffer b, f32 x, f32 y, f32 z, u32 apply) override;
    void SetVelocity(Buffer b, f32 x, f32 y, f32 z, u32 apply) override;
    void SetRolloffCurve(Buffer b, const f32* points, u32 count, u32 apply) override;
    void SetI3DL2Source(Buffer b, const std::array<s32, 9>& params, u32 apply) override;
    // Movie audio uses the same VP packet path as music.
    std::unique_ptr<sound::SoundStream> CreateStream(const std::vector<u8>& format, StreamCallback completed) override;
    s32 SynchPlayback() override;
    void DoStreamWork() override;

    struct Impl;

  private:
    std::unique_ptr<Impl> impl_;
};
} // namespace fut::platform
