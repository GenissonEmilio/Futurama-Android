#include "render/nixbox/NiXBoxRenderer.h"
#include "render/xd3d/XD3DDevice.h"

#include <cstdio>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {
using namespace fut;
using namespace fut::render::xd3d;

int failures = 0;
void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

class LifetimeDevice : public XD3DDevice {
public:
    std::unordered_set<Resource*> gpuViews;
    std::vector<Resource*> retired;
    Resource* newestTexture = nullptr;
    Resource* newestSurface = nullptr;
    Resource* newestDepth = nullptr;

    Resource* CreateTexture2(u32 w, u32 h, u32 levels, u32 usage, u32 format) override {
        return newestTexture = XD3DDevice::CreateTexture2(w, h, levels, usage, format);
    }
    Resource* GetSurfaceLevel2(Resource* texture, u32 level) override {
        return newestSurface = XD3DDevice::GetSurfaceLevel2(texture, level);
    }
    Resource* CreateStandAloneSurface(u32 w, u32 h, u32 format) override {
        return newestDepth = XD3DDevice::CreateStandAloneSurface(w, h, format);
    }
    size_t PayloadCapacity() const {
        size_t bytes = 0;
        for (const auto& r : resources_) bytes += r->data.capacity();
        return bytes;
    }
protected:
    void OnResourceReleased(Resource* r) override {
        Check(std::this_thread::get_id() == owner_, "release hook runs on the owning render thread");
        Check(r->released && !r->refs && !r->bindings, "hook sees final lifetime transition");
        Check(seen_.insert(r).second, "each handle retires exactly once");
        Check(!r->parent || !r->parent->released, "surface hook can still access its live parent");
        gpuViews.erase(r);
        retired.push_back(r);
    }
private:
    const std::thread::id owner_ = std::this_thread::get_id();
    std::unordered_set<Resource*> seen_;
};

void ReferencesAndLookup() {
    LifetimeDevice d;
    Resource* vb = d.CreateVertexBuffer2(1024 * 1024);
    const auto* bytes = vb->data.data();
    const std::string id = vb->id;
    vb->source = &d;
    d.gpuViews.insert(vb);
    Check(d.FindResource(bytes, ResourceKind::kVertexBuffer) == vb, "lookup includes first byte");
    Check(d.FindResource(bytes + vb->data.size() - 1, ResourceKind::kVertexBuffer) == vb, "lookup includes last byte");
    Check(!d.FindResource(bytes + vb->data.size(), ResourceKind::kVertexBuffer), "lookup excludes one-past-end");
    Check(!d.FindResource(bytes, ResourceKind::kIndexBuffer), "lookup checks resource kind");
    Check(!d.FindResource(nullptr, ResourceKind::kVertexBuffer), "null lookup");
    d.AddRef(vb);
    d.Release(vb);
    Check(vb->refs == 1 && !vb->released && !vb->data.empty(), "non-final Release retains payload");
    d.Release(vb);
    Check(vb->refs == 0 && vb->released && vb->data.capacity() == 0, "final Release returns CPU allocation");
    Check(!vb->source && d.gpuViews.empty(), "final Release clears source and backend views");
    Check(!d.FindResource(bytes, ResourceKind::kVertexBuffer), "released payload removed from lookup");
    Check(!d.Lock2(vb, 0), "retired handle cannot be locked");
    d.Release(vb);
    d.AddRef(vb);
    Check(vb->released && !vb->refs && d.retired.size() == 1, "stale Release/AddRef do not revive handle");
    Resource* next = d.CreateVertexBuffer2(1);
    Check(next != vb && id == "vb1" && next->id == "vb2" && vb->id == id, "stable handles and monotonic IDs");
    d.Release(next);
}

void SurfacesAndBindings() {
    LifetimeDevice d;
    Resource* texture = d.CreateTexture2(16, 8, 3, 0, 0x24); // YUY2 needs no default.xbe format table
    Resource* surface = d.GetSurfaceLevel2(texture, 1);
    Check(surface && surface->width == 8 && surface->height == 4 && texture->refs == 2, "surface owns one parent ref");
    Check(d.LockRect(surface) == texture->data.data() + 16 * 8 * 2, "surface lock addresses its mip");
    Check(!d.GetSurfaceLevel2(texture, 3), "invalid mip rejected without a parent ref");
    d.SetTexture(0, texture);
    d.SetTexture(1, texture);
    d.SetTexture(0, texture);
    Check(texture->bindings == 2 && texture->refs == 2, "duplicate stage bind has no extra hold or visible AddRef");
    d.SetRenderTarget(surface, nullptr);
    d.Release(texture);
    d.Release(surface);
    Check(!surface->released && surface->refs == 0 && texture->refs == 1, "bound surface retains parent after caller Release");
    Check(d.LockRect(surface) != nullptr, "bound surface stays usable");
    d.SetRenderTarget(d.backBuffer(), d.depthBuffer());
    Check(surface->released && !surface->parent && texture->refs == 0 && !texture->released, "unbound surface drops parent ref once");
    d.SetTexture(0, nullptr);
    Check(!texture->released && texture->bindings == 1, "second stage keeps texture alive");
    d.SetTexture(1, nullptr);
    Check(texture->released && texture->data.capacity() == 0, "last stage unbind returns texture allocation");
    d.SetTexture(0, texture);
    Check(!d.textures[0] && !d.LockRect(surface), "stale cached handles cannot be rebound or locked");

    Resource* vb = d.CreateVertexBuffer2(64);
    Resource* ib = d.CreateIndexBuffer2(32);
    d.SetStreamSource(0, vb, 16);
    d.SetIndices(ib, 7);
    const u16* indexBytes = d.indexData;
    d.Release(vb);
    d.Release(ib);
    d.SetStreamSource(1, nullptr, 0);
    Check(!vb->released && !ib->released && d.indexData == indexBytes, "stream/index bindings retain bytes; other streams ignored");
    Check(d.FindResource(indexBytes + 1, ResourceKind::kIndexBuffer) == ib, "bound index data remains searchable");
    d.SetStreamSource(0, nullptr, 0);
    d.SetIndices(nullptr, 0);
    Check(vb->released && ib->released && !d.indexData && !d.indices && !d.streamSource, "unbind retires buffers and clears index pointer");

    Resource* a = d.CreateStandAloneSurface(16, 16, 0x2A);
    Resource* b = d.CreateStandAloneSurface(16, 16, 0x2A);
    d.SetRenderTarget(a, b);
    d.Release(a);
    d.Release(b);
    d.SetRenderTarget(b, a);
    Check(!a->released && !b->released && d.renderTarget == b && d.depthStencil == a, "target pair swap acquires both before dropping old holds");
    d.SetRenderTarget(d.backBuffer(), d.depthBuffer());
    Check(a->released && b->released, "target pair unbind retires both");
}

void ManagerAndRenderedTargets() {
    LifetimeDevice d;
    {
        fut::render::nixbox::NiXBoxTextureManager manager(d);
        fut::ni::NiSourceTexture source;
        Resource* texture = d.CreateTexture2(8, 8, 1, 0, 0x24);
        manager.Replace(source, texture);
        d.Release(texture);
        manager.Replace(source, texture);
        Check(!texture->released && texture->refs == 1, "self replacement preserves manager's sole reference");
        manager.Purge(source);
        Check(texture->released, "manager purge releases replacement");
        Resource* retained = d.CreateTexture2(8, 8, 1, 0, 0x24);
        manager.Replace(source, retained);
        d.Release(retained);
    }
    Check(d.newestTexture->released, "manager destruction releases registered textures");
    fut::ni::NiRenderedTexture survivor;
    {
        fut::render::nixbox::NiXBoxRenderer renderer(d);
        for (unsigned i = 0; i < 32; ++i) {
            fut::ni::NiRenderedTexture texture;
            texture.width = texture.height = 32;
            Check(renderer.CreateRenderedTextureData(texture), "rendered texture created");
            Resource* color = d.newestSurface;
            Resource* depth = d.newestDepth;
            Resource* parent = d.newestTexture;
            Check(parent->refs == 2, "rendered data and surface own exactly two texture refs");
            Check(renderer.SetRenderTarget(&texture), "rendered texture made current");
            renderer.PurgeTexture(texture);
            Check(d.renderTarget == d.backBuffer() && d.depthStencil == d.depthBuffer(), "purging active target rebinds framebuffers");
            Check(color->released && depth->released && parent->released, "rendered purge releases color, depth and texture");
        }
        survivor.width = survivor.height = 32;
        renderer.CreateRenderedTextureData(survivor);
        renderer.SetRenderTarget(&survivor);
    }
    Check(d.newestTexture->released && d.newestSurface->released && d.newestDepth->released,
          "renderer destruction releases every resource of a surviving texture");
    Check(d.PayloadCapacity() == 0, "renderer/manager teardown leaves no CPU texture payloads");
}

void Stress() {
    LifetimeDevice d;
    constexpr unsigned batchSize = 64, batches = 256;
    std::vector<Resource*> batch;
    batch.reserve(batchSize);
    for (unsigned cycle = 0; cycle < batches; ++cycle) {
        for (unsigned i = 0; i < batchSize; ++i) {
            Resource* r = d.CreateVertexBuffer2(16 * 1024);
            d.gpuViews.insert(r);
            batch.push_back(r);
        }
        for (Resource* r : batch) {
            Check(d.FindResource(r->data.data() + 8192, ResourceKind::kVertexBuffer) == r, "stress lookup finds active interval after many retired handles");
            d.SetStreamSource(0, r, 16);
            d.Release(r);
            Check(!r->released, "stress binding holds released client resource");
        }
        d.SetStreamSource(0, nullptr, 0);
        for (Resource* r : batch) Check(r->released && r->data.capacity() == 0, "stress retired allocation returned");
        Check(d.gpuViews.empty(), "stress backend cache has no retired entries");
        batch.clear();
    }
    Check(d.PayloadCapacity() == 0 && d.retired.size() == batchSize * batches, "stress payload/cache usage returns to baseline");
}
}

int main() {
    ReferencesAndLookup();
    SurfacesAndBindings();
    ManagerAndRenderedTargets();
    Stress();
    std::printf("Resource lifetime checks: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
