# Android GLES renderer

`GLES3Device` executes the Xbox fixed-function state exposed by `XD3DDevice`.
Its EGL context belongs to the game rendering thread. `SetWindow(nullptr, error)`
switches to a retained pbuffer; reattaching a window preserves GPU resources.
Repeated attachment of the same native window reuses its EGL surface. Replacing
a window first switches to the pbuffer and destroys the previous window surface,
because different native wrappers can share one buffer producer. A failed new
attachment leaves the context on the pbuffer, with its GPU resources retained.
The internal Xbox target remains 640×480 and presentation preserves its 4:3 ratio.

Matrices retain Xbox row-major bytes. GLSL interprets these as transposed matrices,
so shaders multiply matrix by vector. The vertex shader converts D3D clip depth
to GLES clip depth. Rendered texture sampling flips Y relative to uploaded textures.

Keep `glFrontFace(GL_CW)`: D3D11 uses `FrontCounterClockwise=FALSE`. The movie quad
projects to CCW clip coordinates and Xbox `CULLMODE=0x900` culls the opposite
winding. Using `GL_CCW` instead culled the entire movie polygon on the phone.
Presentation outputs alpha 1 because the Xbox backbuffer is X8R8G8B8.

## Validation

- NDK r30, ARM64/API 26: C++20 syntax compilation with `-Wall -Wextra` passes.
- Headless Chrome ANGLE/SwiftShader: both GLSL ES 3.0 shaders compile and link.
- Linked uniform block reports 1920 bytes, matching the C++ layout assertion.
- Uploaded `DrawCB` and canonical vertices render the expected red XYZRHW test
  triangle; readback is RGBA `(255, 0, 0, 255)` with GL error 0.
- A CCW quad with the Xbox clockwise-culling state produces the same successful
  readback, covering the movie polygon regression.
- Physical Android phone: the 20th Century Fox introduction renders visibly
  behind touch controls after the winding fix. Menu/gameplay coverage is separate.

## Known limits

The GLES backend retains the D3D11 backend's unimplemented NV2A programmable pixel
shaders, bump-environment operations, vertex blending and point sprites. Texture
border addressing currently clamps to the edge; sampler mip LOD bias is reported
but not applied. BC1/BC2/BC3 textures decompress to RGBA8 on the CPU, avoiding a
dependency on optional S3TC support. NV2A arithmetic precision is approximated.
