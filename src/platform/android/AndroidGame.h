#pragma once
#include "platform/android/AndroidInput.h"
#include <jni.h>
#include <android/native_window.h>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace fut::platform {
class AndroidGame {
public:
    using Message = std::function<void(const std::string&, bool)>;
    AndroidGame(std::string disc, std::string saves, ANativeWindow* window, Message message);
    ~AndroidGame();
    // Acquires its own reference; returns only when the render thread no longer uses the old surface.
    void SetWindow(ANativeWindow* window);
    void Pause(bool paused);
    AndroidInput input;
private:
    void Run();
    std::string disc_, saves_;
    Message message_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
    ANativeWindow* requestedWindow_ = nullptr;
    unsigned requested_ = 1, applied_ = 0;
    bool paused_ = false, stopping_ = false, finished_ = false;
};
std::string ValidateAndroidDisc(const std::string& source);
}
