#include "platform/android/AndroidGame.h"
#include "platform/android/AndroidSoundBackend.h"
#include "platform/android/MovieService.h"
#include "core/XbeImage.h"
#include "core/fs/Vfs.h"
#include "game/app/App.h"
#include "game/app/SaveStorage.h"
#include "game/sound/GameSound.h"
#include "game/text/TextObject.h"
#include "render/nixbox/NiXBoxRenderer.h"
#include "render/gles/GLES3Device.h"
#include <android/native_window_jni.h>
#include <android/log.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace fut::platform {
std::string ValidateAndroidDisc(const std::string& source) {
    fs::Vfs vfs;
    std::string error;
    if (!vfs.Open(source, error)) return error;
    std::vector<u8> data;
    if (!vfs.ReadFile("default.xbe", data)) return "default.xbe was not found. Select your Futurama Xbox NTSC-U or PAL disc image.";
    xbe::XbeImage probe;
    if (!probe.Load(std::move(data), &error)) return error;
    return {};
}
AndroidGame::AndroidGame(std::string disc, std::string saves, ANativeWindow* window, Message message)
    : disc_(std::move(disc)), saves_(std::move(saves)), message_(std::move(message)), requestedWindow_(window) {
    if (window) ANativeWindow_acquire(window);
    thread_ = std::thread(&AndroidGame::Run, this);
}
AndroidGame::~AndroidGame() {
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
    if (requestedWindow_) ANativeWindow_release(requestedWindow_);
}
void AndroidGame::SetWindow(ANativeWindow* window) {
    if (window) ANativeWindow_acquire(window);
    std::unique_lock<std::mutex> lock(mutex_);
    if (requestedWindow_) ANativeWindow_release(requestedWindow_);
    requestedWindow_ = window;
    const unsigned serial = ++requested_;
    wake_.notify_all();
    wake_.wait(lock, [&] { return applied_ >= serial || finished_; });
}
void AndroidGame::Pause(bool paused) {
    { std::lock_guard<std::mutex> lock(mutex_); paused_ = paused; }
    if (paused) input.Reset();
    wake_.notify_all();
}
void AndroidGame::Run() {
    try {
        fs::Vfs vfs;
        std::string error;
        if (!vfs.Open(disc_, error)) throw std::runtime_error(error);
        std::vector<u8> xbeData;
        if (!vfs.ReadFile("default.xbe", xbeData)) throw std::runtime_error("default.xbe not found in the disc image");
        if (!xbe::LoadGameImage(std::move(xbeData), &error)) throw std::runtime_error(error);
        const auto& xbeFile = xbe::GameImage().Bytes();
        render::xd3d::GLES3Device device;
        // Start on a pbuffer: lifecycle requests are applied below on the context's owner thread.
        if (!device.Init(nullptr, error)) throw std::runtime_error(error);
        render::nixbox::NiXBoxRenderer renderer(device);
        AndroidSoundBackend::Options options;
        options.stepStatus = true;
        AndroidSoundBackend backend(options);
        const bool offline = !backend.Init();
        if (offline) { backend.InitOffline(); message_("Audio output unavailable; running silently.", false); }
        std::vector<u8> effects;
        if (vfs.ReadFile("xbox/dsstdfx.bin", effects)) backend.SetEffectsImage(effects);
        backend.LoadXbeTables(xbeFile);
        MovieService movies(vfs, &backend, xbeFile);
        sound::XboxSound sound(&backend, [&](const std::string& path, std::vector<u8>& out) { return vfs.ReadFile(path, out); });
        game::app::HostSaveStorage saves{std::filesystem::path(saves_)};
        saves.readDisc = [&](const std::string& path, std::vector<u8>& out) { return vfs.ReadFile(path, out); };
        input::GamepadState pad;
        game::app::AppServices services;
        services.readFile = [&](const std::string& path, std::vector<u8>& out) { return vfs.ReadFile(path, out); };
        services.streamFile = services.readFile;
        services.readPad = [&](u32 port) -> const input::GamepadState* {
            if (port) return nullptr;
            pad = input.Snapshot(); return &pad;
        };
        services.deviceChanges = [](u32& inserted, u32& removed) { inserted = removed = 0; return false; };
        services.renderer = &renderer; services.sound = &sound; services.movies = &movies; services.saves = &saves;
        services.qpf = 1000000000; services.pal = vfs.IsPal();
        text::CreateAppFont(services.readFile);
        game::app::App app(services);
        app.qpc = 0;
        if (!app.Init()) throw std::runtime_error("The game could not initialize. Check that the selected disc is a supported Futurama Xbox image.");
        message_("", false);
        using Clock = std::chrono::steady_clock;
        auto previous = Clock::now();
        bool audioPaused = false;
        std::vector<s32> droppedAudio;
        for (s32 frame = 0;;) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (stopping_) break;
                if (applied_ != requested_) {
                    if (!device.SetWindow(requestedWindow_, error)) throw std::runtime_error(error);
                    applied_ = requested_;
                    wake_.notify_all();
                }
                const bool suspend = paused_ || !requestedWindow_;
                if (suspend) {
                    if (!audioPaused) { movies.SetPaused(true); backend.SetPaused(true); audioPaused = true; }
                    wake_.wait(lock, [&] { return stopping_ || applied_ != requested_ || (!paused_ && requestedWindow_); });
                    previous = Clock::now();
                    continue;
                }
                if (audioPaused) { movies.SetPaused(false); backend.SetPaused(false); audioPaused = false; }
            }
            const auto now = Clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - previous).count();
            previous = now;
            app.qpc += static_cast<u64>(std::clamp<int64_t>(elapsed, 0, 250000000));
            app.frame = frame++;
            if (!app.Frame()) { message_("The game has finished. Reopen the app to play again.", false); break; }
            if (offline) { droppedAudio.clear(); backend.RenderFrames(25 * app.lastSteps, droppedAudio); }
            // eglSwapInterval is the primary pacing; a tiny yield also covers drivers that ignore it.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // Keep teardown GL operations valid even when Android has already removed its surface.
        device.SetWindow(nullptr, error);
    } catch (const std::exception& error) {
        __android_log_print(ANDROID_LOG_ERROR, "Futurama", "%s", error.what());
        message_(error.what(), true);
    } catch (...) {
        message_("An unexpected native game error occurred. See Android logcat for details.", true);
    }
    { std::lock_guard<std::mutex> lock(mutex_); finished_ = true; }
    wake_.notify_all();
}
}

namespace {
JavaVM* vm = nullptr;
jobject activity = nullptr;
std::unique_ptr<fut::platform::AndroidGame> game;
std::string Utf8(JNIEnv* env, jstring string) {
    if (!string) return {};
    const char* value = env->GetStringUTFChars(string, nullptr);
    if (!value) return {};
    std::string result(value); env->ReleaseStringUTFChars(string, value); return result;
}
void Message(const std::string& text, bool error) {
    JNIEnv* env = nullptr;
    const bool attach = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK;
    if (attach && vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
    jclass type = env->GetObjectClass(activity);
    jmethodID method = env->GetMethodID(type, error ? "onNativeError" : "onNativeStatus", "(Ljava/lang/String;)V");
    jstring value = env->NewStringUTF(text.c_str());
    if (method && value) env->CallVoidMethod(activity, method, value);
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    env->DeleteLocalRef(value); env->DeleteLocalRef(type);
    if (attach) vm->DetachCurrentThread();
}
}
extern "C" JNIEXPORT jstring JNICALL Java_com_genisson_futuramarecomp_MainActivity_nativeValidateIso(JNIEnv* env, jclass, jstring path) {
    try { return env->NewStringUTF(fut::platform::ValidateAndroidDisc(Utf8(env, path)).c_str()); }
    catch (const std::exception& error) { return env->NewStringUTF(error.what()); }
    catch (...) { return env->NewStringUTF("Could not read the disc image."); }
}
extern "C" JNIEXPORT void JNICALL Java_com_genisson_futuramarecomp_MainActivity_nativeStart(JNIEnv* env, jobject self, jobject surface, jstring path, jstring saves) {
    env->GetJavaVM(&vm);
    ANativeWindow* window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    try {
        if (!game) {
            activity = env->NewGlobalRef(self);
            game = std::make_unique<fut::platform::AndroidGame>(Utf8(env, path), Utf8(env, saves), window, Message);
        } else game->SetWindow(window);
    } catch (const std::exception& error) {
        if (activity) Message(error.what(), true);
    }
    if (window) ANativeWindow_release(window);
}
extern "C" JNIEXPORT void JNICALL Java_com_genisson_futuramarecomp_MainActivity_nativeSurface(JNIEnv* env, jobject, jobject surface) {
    ANativeWindow* window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    if (game) game->SetWindow(window);
    if (window) ANativeWindow_release(window);
}
extern "C" JNIEXPORT void JNICALL Java_com_genisson_futuramarecomp_MainActivity_nativeStop(JNIEnv* env, jobject) {
    game.reset();
    if (activity) { env->DeleteGlobalRef(activity); activity = nullptr; }
}
extern "C" JNIEXPORT void JNICALL Java_com_genisson_futuramarecomp_MainActivity_nativePause(JNIEnv*, jobject, jboolean paused) { if (game) game->Pause(paused); }
extern "C" JNIEXPORT void JNICALL Java_com_genisson_futuramarecomp_MainActivity_nativeKey(JNIEnv*, jobject, jint key, jboolean down) { if (game) game->input.Key(key, down); }
extern "C" JNIEXPORT void JNICALL Java_com_genisson_futuramarecomp_MainActivity_nativeAxes(JNIEnv*, jobject, jfloat lx, jfloat ly, jfloat rx, jfloat ry, jfloat lt, jfloat rt, jfloat hx, jfloat hy) { if (game) game->input.Axes(lx, ly, rx, ry, lt, rt, hx, hy); }
extern "C" JNIEXPORT void JNICALL Java_com_genisson_futuramarecomp_MainActivity_nativeTouch(JNIEnv*, jobject, jint buttons, jfloat lx, jfloat ly, jfloat rx, jfloat ry) { if (game) game->input.Touch(buttons, lx, ly, rx, ry); }
