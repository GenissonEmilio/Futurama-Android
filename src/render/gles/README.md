# Android GLES renderer

`GLES3Device` executes the Xbox fixed-function state exposed by `XD3DDevice`.
Its EGL context belongs to the game rendering thread. `SetWindow(nullptr, error)`
switches to a retained pbuffer; reattaching a window preserves GPU resources.
Repeated attachment of the same native window reuses its EGL surface. Replacing
a window first switches to the pbuffer and destroys the previous window surface,
because different native wrappers can share one buffer producer. A failed new
attachment leaves the context on the pbuffer, with its GPU resources retained.
Xbox viewport and screen-space coordinates remain logically 640×480. The Android
game startup defaults to 2× supersampling: the main color and depth targets are
1280×960, with clears and screen-space geometry scaled consistently. Auxiliary
render targets retain their own dimensions. Presentation preserves the 4:3 ratio.

Matrices retain Xbox row-major bytes. GLSL interprets these as transposed matrices,
so shaders multiply matrix by vector. The vertex shader converts D3D clip depth
to GLES clip depth. Rendered texture sampling flips Y relative to uploaded textures.

Draw uploads use three distinct VBO/IBO/UBO banks with one VAO per bank. A fence
seals each bank after its final draw; CPU writes reuse it only after that fence
signals. Allocations are lazy (36 MiB at the initial three-bank capacities),
bounded to 60 MiB, and storage is never orphaned while draws still reference it.
Disjoint mapped regions preserve upload performance. A 14,400-draw pixel test
detects the corruption that occurred when the previous single arenas were
orphaned while unsynchronized uploads were in flight.

Keep `glFrontFace(GL_CW)`: D3D11 uses `FrontCounterClockwise=FALSE`. The movie quad
projects to CCW clip coordinates and Xbox `CULLMODE=0x900` culls the opposite
winding. Using `GL_CCW` instead culled the entire movie polygon on the phone.
Presentation outputs alpha 1 because the Xbox backbuffer is X8R8G8B8.

Texture conversion handles 16-, 24- and 32-bit pixel data with channel masks.
Explicit texture writes advance the cached revision even when every texel is zero,
so updates preserve transparent alpha and clear previous RGB values. Fragment
sampling applies `MIPMAPLODBIAS`; sampler LOD bounds honor `MAXMIPLEVEL`, including
`MIPFILTER=NONE`. Border addressing uses the configured border color through
`GL_EXT_texture_border_clamp` or `GL_OES_texture_border_clamp` when available.
GPUs without either extension fall back to clamp-to-edge.

## Validation

- NDK r30, ARM64/API 26: C++20 syntax compilation with `-Wall -Wextra` passes.
- Headless Chrome ANGLE/SwiftShader: both GLSL ES 3.0 shaders compile and link.
- The current `DrawCB` std140 layout is 1936 bytes, matching the C++ layout
  assertion and including the per-stage mip LOD bias vector.
- Uploaded `DrawCB` and canonical vertices render the expected red XYZRHW test
  triangle; readback is RGBA `(255, 0, 0, 255)` with GL error 0.
- A CCW quad with the Xbox clockwise-culling state produces the same successful
  readback, covering the movie polygon regression.
- Physical Android phone: the 20th Century Fox introduction renders visibly
  behind touch controls after the winding fix. Menu/gameplay coverage is separate.
- Eight native Android suite executions cover the portable core, saves, resource
  lifetimes, filesystem safety, texture conversion, GLES, raster and raster
  `--scaled`. Raster fixtures check depth, alpha, culling, render-to-texture,
  texture updates and sampler behavior; the scaled fixture checks the 1280×960
  main targets while keeping 640×480 logical geometry and clear rectangles.
  Native border-color checks are skipped when the required extension is absent.
- Signed APK phone checks and measured Release performance are recorded in
  [RELEASE_0.2.0.md](../../../docs/RELEASE_0.2.0.md). Controlled fixtures and short
  gameplay checks do not establish campaign-wide graphical equivalence.

## Known limits

The GLES backend retains the D3D11 backend's unimplemented NV2A programmable pixel
shaders, bump-environment operations, vertex blending and point sprites. Native
texture border addressing depends on the GPU extensions described above.
BC1/BC2/BC3 textures decompress to RGBA8 on the CPU, avoiding a dependency on
optional S3TC support. NV2A arithmetic precision is approximated. Supersampling
increases main-target memory and GPU work; campaign-wide behavior and sustained
performance have not been established.
