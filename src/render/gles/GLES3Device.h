// Android OpenGL ES 3 execution backend for the recorded Xbox D3D8 state.
#pragma once
#include "render/xd3d/XD3DDevice.h"
#include <memory>
#include <string>
struct ANativeWindow;
namespace fut::render::xd3d {
class GLES3Device : public XD3DDevice {
public:
    GLES3Device();
    ~GLES3Device() override;
    // All methods, including destruction and surface replacement, run on the render thread.
    bool Init(ANativeWindow* window, std::string& error, float renderScale = 1.0f);
    // nullptr detaches the window while preserving the context and all game GPU resources.
    bool SetWindow(ANativeWindow* window, std::string& error);
    u32 draws() const { return draws_; }
    void Clear(u32 count, const D3DRECT* rects, u32 flags, u32 color, f32 z, u32 stencil) override;
    void Swap(u32 flags) override;
    void DrawVertices(u32 primitive, u32 start, u32 count) override;
    void DrawIndexedVertices(u32 primitive, u32 count, const u16* data) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    u32 draws_ = 0;
    void Draw(u32 primitive, const u16* list, u32 count, u32 start);
    void OnResourceReleased(Resource* r) override;
};
}
