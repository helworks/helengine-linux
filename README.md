# HelEngine Linux

Native Linux backend under development: SDL3 for window/events/input and Vulkan for rendering, consuming generated C++20 core and cooked assets. The engine player requires Vulkan 1.2 because the existing authored-shader compiler targets Vulkan 1.2/SPIR-V 1.5. The independent UI diagnostic uses a Vulkan 1.1 subset. Build from the actual `main` checkout. No worktrees, snapshots or generated-source rewrites are used.

The GPU diagnostic is **not** the DemoDisc player. The real `helengine_linux` target links generated Core, scene catalog and input providers with complete cook/codegen runtime manifests. The actual DemoDisc menu passed 10 and 300 offscreen frames on GTX 1070 with zero Vulkan validation errors. SDL virtual-gamepad replay also passed selection, panel navigation and an actual transition to `software_path_tracer`, both offscreen and under isolated Xvfb/X11. The baked menu retains all 44 buttons and all 15 catalog entries. Native indexed mesh/material/depth GPU tests pass; integration of the authored 3D scenes and shadow resources remains in progress. The full set of 14 rendering scenes is not yet validated. Numeric policy options remain 16/16, 32/32 and 64/64, with fixed float32 GPU buffers.

## Reproducible native diagnostics

On the authorized Linux host, from `/home/helena/dev/helworks/helengine-linux`:

```sh
source /home/helena/.config/helengine/env.sh
python3 scripts/prepare-dependencies.py
python3 tests/test_configuration.py
python3 scripts/build.py --diagnostics --profile Debug --test
```

`dependencies.lock.json` pins official Ubuntu 26.04 amd64 packages, including SDL 3.4.2, Vulkan headers and glslang, by version and SHA256. Downloads are verified before extraction; no sudo/system installation is needed. Persistent tools install under `/home/helena/dev/helworks/tools/linux-native`. The loader and NVIDIA driver are supplied by the system; the binary also dynamically uses SDL3. Normal package resolution supplies the loader; the local diagnostic build uses the explicit system loader and extracted package prefix.

The Profiler dependency is the official [Tracy v0.13.1 source](https://github.com/wolfpld/tracy/tree/05cceee0df3b8d7c6fa87e9638af311dbabc63cb), locked to commit `05cceee0df3b8d7c6fa87e9638af311dbabc63cb`. Preparation clones it into `/home/helena/dev/helworks/tools/tracy`, checks the exact commit and rejects dirty or mismatched existing checkouts. Profiler CMake can receive an explicit `HELENGINE_TRACY_ROOT` to this pinned checkout.

The managed content cooker reuses the engine's real `VulkanShaderBackend` and its HLSL-to-SPIR-V compiler. The lock includes Ubuntu `libshaderc1` 2026.1-1; preparation creates the relative loader alias `/home/helena/dev/helworks/tools/linux-native/usr/lib/x86_64-linux-gnu/libshaderc_shared.so` pointing to `libshaderc.so.1`, matching Silk's expected filename. Supply `LD_LIBRARY_PATH=/home/helena/dev/helworks/tools/linux-native/usr/lib/x86_64-linux-gnu` only for the cooker/validation process; do not set it globally for unrelated tools. Shaderc is a build dependency and is not bundled into the native runtime package.

Output always overwrites `/tmp/helengine/linux` on Linux and `C:\temp\helengine\linux` for managed verification on Windows. The primary artifact is `/tmp/helengine/linux/helengine_linux_gpu_test`. SPIR-V outputs are `/tmp/helengine/linux/shaders/quad.vert.spv` and `/tmp/helengine/linux/shaders/quad.frag.spv`.

Verified on NVIDIA GeForce GTX 1070, driver 580.178.04: real sampled-image quads, ordered alpha composition, nested clipping, target resize, dynamic atlas region/pitch uploads, UV association, rounded shape fill/border/corners, texture release and zero-drawable deferral, checked by GPU readback. Counted Khronos validation reports zero errors. The offscreen diagnostic creates no window and captures no desktop image. The SDL window/swapchain path passed creation, presentation and resize under isolated Xvfb/X11 on the same GPU; Wayland remains unvalidated. Optional validation is explicit; requesting an absent Khronos validation layer fails contextually. Native SDL device selection rejects devices below Vulkan 1.2; no ray-tracing extension is requested. Submission currently uses fences and synchronous uploads/readback for correctness; throughput has not been measured.

## Pipeline interface

CMake accepts `HELENGINE_CORE_CPP_ROOT`, `HELENGINE_CODE_ROOT`, `HELENGINE_CONTENT_ROOT` and `HELENGINE_NATIVE_PROFILE=Debug|Release|Profiler`. A player build requires the generated `helcpp_config.hpp`, desktop input contract and complete runtime manifests; `HELENGINE_LINUX_BUILD_PLAYER=OFF` explicitly selects diagnostics. Profiler requires the exact locked Tracy checkout and enables real generated CPU scopes, component/entity counters and Tracy frame markers; Debug and Release use the null profiling sink. Complete Profiler capture validation is pending.

The builder project is `C:\dev\helworks\helengine-linux\builder\helengine.linux.builder.csproj`; its assembly is `helengine.linux.builder.dll` and entry type is `helengine.linux.builder.LinuxPlatformAssetBuilder`. Set `HELENGINE_LINUX_REPOSITORY_ROOT` to the actual sibling checkout when loading the assembly outside its repository tree. Set `HELENGINE_LINUX_NATIVE_PREFIX=/home/helena/dev/helworks/tools/linux-native/usr` for the locked user-level tool prefix. Its native executor uses structured process arguments and preserves child failure and cancellation. The real Linux CLI completed cooking, codegen, runtime manifests, native compilation and package staging. Every declared `CookedArtifacts` entry is required and copied at its exact runtime-relative identity, including dependencies absent from `LooseAssets`.

```powershell
dotnet run --project C:\dev\helworks\helengine-linux\builder.tests --artifacts-path C:\temp\helengine\linux\managed
```

The generated 2D adapter consumes `RenderCommandListBuilder2D.Build(camera.RenderQueue2D)` in command order: `ClipPush`, `ClipPop`, `TexturedQuad`, `GlyphQuad` and `RoundedRect`. Quad geometry/pivots and source rectangles remain engine-owned. Gamepad snapshots alternate two native arrays because the generated input system retains both current and previous frames.

## Actual menu navigation verification

After the native CLI produces `/tmp/helengine/linux/package`, run the player from the fixed native output or pass its packaged absolute path:

```sh
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json VK_LAYER_PATH=/home/helena/dev/helworks/tools/linux-native/usr/share/vulkan/explicit_layer.d LD_LIBRARY_PATH=/home/helena/dev/helworks/tools/linux-native/usr/lib/x86_64-linux-gnu python3 /home/helena/dev/helworks/helengine-linux/tests/test_navigation.py --player /tmp/helengine/linux/helengine_linux --content /tmp/helengine/linux/package
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json VK_LAYER_PATH=/home/helena/dev/helworks/tools/linux-native/usr/share/vulkan/explicit_layer.d LD_LIBRARY_PATH=/home/helena/dev/helworks/tools/linux-native/usr/lib/x86_64-linux-gnu python3 /home/helena/dev/helworks/helengine-linux/tests/test_navigation.py --player /tmp/helengine/linux/helengine_linux --content /tmp/helengine/linux/package --xvfb
```

The committed replay at `/home/helena/dev/helworks/helengine-linux/tests/input/menu-scene-navigation.txt` supplies frame/down-or-up/SDL-scancode-name lines. A `pad:` prefix selects SDL canonical gamepad mapping names, such as `pad:a`, `pad:b` and `pad:dpdown`. An explicit `--input-script` creates an SDL virtual gamepad, whose ordinary SDL snapshot passes through the same `SdlInputBackend.CaptureFrame` and generated Core as physical input. Tests read actual menu state and scene transitions; they never call menu actions or scene-loading APIs. No screenshot is captured.
