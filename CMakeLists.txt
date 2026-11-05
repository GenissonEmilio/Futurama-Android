# Native Futurama: the original Windows target and the Android ARM64 platform.
# Game data comes from the user's disc at runtime, never from the source tree.
cmake_minimum_required(VERSION 3.25)
project(FuturamaRecomp LANGUAGES C CXX)

option(FUT_BUILD_TESTS "Build portable runtime regression tests" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -ffp-contract=off -fno-strict-aliasing)
endif()
# The static C / C++ runtime (/MT): the exe needs no VC++ redistributable, only DLLs that ship with Windows.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(fut_core STATIC
    src/render/TextureDecode.cpp
    src/core/FatalError.cpp
    src/core/XbeImage.cpp
    src/core/memory/MemoryManager.cpp
    src/core/fs/ImgArchive.cpp
    src/core/fs/Xdvdfs.cpp
    src/core/fs/Vfs.cpp
    src/game/script/ScriptVM.cpp
    src/game/script/ScriptNatives.cpp
    src/game/script/GameNatives.cpp
    src/engine/ni/NiStream.cpp
    src/engine/ni/NiObjects.cpp
    src/engine/ni/NiSceneGraph.cpp
    src/engine/ni/NiClone.cpp
    src/engine/ni/NiCollision.cpp
    src/engine/ni/NiCull.cpp
    src/engine/ni/NiOutline.cpp
    src/engine/ni/NiAnimation.cpp
    src/engine/ni/NiKeyframeManager.cpp
    src/engine/ni/NiRenderState.cpp
    src/engine/stream/StreamManager.cpp
    src/game/text/LanguageDb.cpp
    src/game/text/TextLayout.cpp
    src/game/text/TextObject.cpp
    src/game/input/Pad.cpp
    src/game/defs/Defs.cpp
    src/game/defs/CharacterDef.cpp
    src/game/defs/IngameEvent.cpp
    src/game/defs/ExplosionDef.cpp
    src/game/defs/EffectDefs.cpp
    src/game/defs/NpcDefs.cpp
    src/game/sound/GameSound.cpp
    src/game/sound/SoundEvents.cpp
    src/game/sound/SoundSystem.cpp
    src/game/level/LevelData.cpp
    src/game/level/LevelLoader.cpp
    src/game/level/LevelAnimation.cpp
    src/game/level/LightManager.cpp
    src/game/level/Intermission.cpp
    src/game/level/Session.cpp
    src/game/level/Hud.cpp
    src/game/level/Dialogue.cpp
    src/game/level/QuickSave.cpp
    src/game/level/Camera.cpp
    src/game/level/ScriptTriggers.cpp
    src/game/level/ClosestPoint.cpp
    src/game/level/Stage.cpp
    src/game/level/TriggerManager.cpp
    src/game/level/StagePreCache.cpp
    src/game/app/App.cpp
    src/game/app/Breakdown.cpp
    src/game/app/CheatCodes.cpp
    src/game/app/Contexts.cpp
    src/game/app/LanguageSelect.cpp
    src/game/app/ErrorOverlay.cpp
    src/game/app/Extras.cpp
    src/game/app/Frontend.cpp
    src/game/app/LoadPanel.cpp
    src/game/app/MemoryCards.cpp
    src/game/app/MenuScene.cpp
    src/game/app/MoviePlayer.cpp
    src/game/app/Options.cpp
    src/game/app/OptionsPanel.cpp
    src/game/app/PauseMenu.cpp
    src/game/app/SaveStorage.cpp
    src/game/object/Emitter.cpp
    src/game/object/Explosion.cpp
    src/game/object/Spell.cpp
    src/game/object/Fire.cpp
    src/game/object/GameObject.cpp
    src/game/object/Impact.cpp
    src/game/object/Ripple.cpp
    src/game/object/RespawnEffect.cpp
    src/game/object/Splash.cpp
    src/game/object/Laser.cpp
    src/game/object/Liquid.cpp
    src/game/object/MarkInstance.cpp
    src/game/object/Character.cpp
    src/game/object/LipSync.cpp
    src/game/object/CharacterUpdate.cpp
    src/game/object/Cannon.cpp
    src/game/object/ChickenWalker.cpp
    src/game/object/PlayerControl.cpp
    src/game/object/Destructable.cpp
    src/game/object/GenericNpc.cpp
    src/game/object/SimpleNpc.cpp
    src/game/object/Moveable.cpp
    src/game/object/Pickup.cpp
    src/game/object/RatRide.cpp
    src/game/object/Static.cpp
    src/game/physics/Physics.cpp
    src/game/data/ConfigFile.cpp
    src/render/xd3d/XD3DTypes.cpp
    src/render/xd3d/XD3DDevice.cpp
    src/render/nixbox/NiXBoxGeometryBuffers.cpp
    src/render/nixbox/NiXBoxRenderer.cpp
    src/render/nixbox/NiXBoxRenderState.cpp
    src/render/nixbox/NiXBoxLightManager.cpp
    src/render/nixbox/NiXBoxTexturePipeline.cpp
    src/audio/Wav.cpp
    src/audio/xdsound/XdsMath.cpp
    src/audio/xdsound/XdsDirectSound.cpp
    src/audio/vp/VpFrontEnd.cpp
    src/audio/vp/Vp.cpp
    src/audio/vp/VpBridge.cpp
    src/audio/vp/VpStream.cpp
    src/audio/dsp/DsImage.cpp
    src/audio/dsp/Dsp56300.cpp
    src/audio/dsp/McpxGp.cpp
    src/audio/dsp/McpxEp.cpp
    src/audio/dsp/AudioDsp.cpp
    src/video/xmv/XmvImage.cpp
    src/video/xmv/Wmv2Decoder.cpp
    src/video/xmv/Wmv2Kernels.cpp
    src/video/xmv/XmvDecoder.cpp
)
target_include_directories(fut_core PUBLIC src)
set_target_properties(fut_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(ANDROID)
    option(FUT_ANDROID_OPTIMIZE_DEBUG "Keep Android debug APKs playable; disable for unoptimized stepping" ON)
    add_library(futurama_android SHARED
        src/platform/android/AndroidGame.cpp
        src/platform/android/AndroidInput.cpp
        src/platform/android/AndroidSoundBackend.cpp
        src/platform/android/MovieService.cpp
        src/render/gles/GLES3Device.cpp
    )
    target_link_libraries(futurama_android PRIVATE fut_core android log EGL GLESv3 aaudio)
    target_link_options(futurama_android PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384)
    if(FUT_ANDROID_OPTIMIZE_DEBUG)
        target_compile_options(fut_core PRIVATE $<$<CONFIG:Debug>:-O2>)
        target_compile_options(futurama_android PRIVATE $<$<CONFIG:Debug>:-O2>)
    endif()
elseif(WIN32)

add_library(fut_render STATIC
    src/platform/win32/Window.cpp
    src/platform/win32/GamepadInput.cpp
    src/platform/win32/Launcher.cpp
    src/platform/win32/XAudio2SoundBackend.cpp
    src/platform/win32/MovieService.cpp
    src/render/xd3d/D3D11Device.cpp
)
target_include_directories(fut_render PRIVATE src)
target_link_libraries(fut_render PUBLIC fut_core)

add_executable(fut_game
    app/game/GameMain.cpp
)
set_target_properties(fut_game PROPERTIES WIN32_EXECUTABLE ON OUTPUT_NAME FuturamaRecomp)
if(MSVC)
    target_link_options(fut_game PRIVATE /ENTRY:mainCRTStartup)
endif()
target_include_directories(fut_game PRIVATE src)
target_link_libraries(fut_game PRIVATE fut_core fut_render d3d11 dxgi d3dcompiler xinput xaudio2)
endif()

if(FUT_BUILD_TESTS)
    enable_testing()
    add_executable(fut_portable_tests tests/PortableRuntimeTests.cpp)
    target_link_libraries(fut_portable_tests PRIVATE fut_core)
    add_test(NAME portable_runtime COMMAND fut_portable_tests)
    foreach(suite IN ITEMS SaveStorage ResourceLifetime FilesystemSafety TextureConversion)
        add_executable(fut_${suite}_tests tests/${suite}Tests.cpp)
        target_link_libraries(fut_${suite}_tests PRIVATE fut_core)
        add_test(NAME ${suite} COMMAND fut_${suite}_tests)
    endforeach()
    if(ANDROID)
        add_executable(fut_android_gles_tests tests/AndroidGlesTests.cpp)
        target_include_directories(fut_android_gles_tests PRIVATE src)
        target_link_libraries(fut_android_gles_tests PRIVATE futurama_android EGL GLESv3)
        add_test(NAME android_gles COMMAND fut_android_gles_tests)
        add_executable(fut_android_raster_tests tests/AndroidRasterTests.cpp)
        target_include_directories(fut_android_raster_tests PRIVATE src)
        target_link_libraries(fut_android_raster_tests PRIVATE futurama_android EGL GLESv3)
        add_test(NAME android_raster COMMAND fut_android_raster_tests)
        add_test(NAME android_scaled COMMAND fut_android_raster_tests --scaled)
    endif()
endif()
