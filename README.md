# HelEngine Linux

Native Linux backend under development: SDL3 for window/events/input and Vulkan 1.1 for rendering, consuming generated C++20 core and cooked assets. Build from the actual `main` checkout. No worktrees, snapshots or generated-source rewrites are used.

The GPU diagnostic is **not** the DemoDisc player. The generated-core provider/renderer bridge is incomplete, so player configuration fails explicitly rather than producing a no-op executable. The menu and all 14 selected rendering scenes remain unvalidated. Metadata retains the engine's full standard material schema and numeric policy options 16/16, 32/32 and 64/64. No menu buttons have been filtered.

## Reproducible native diagnostics

On the authorized Linux host, from `/home/helena/dev/helworks/helengine-linux`:

```sh
source /home/helena/.config/helengine/env.sh
python3 scripts/prepare-dependencies.py
python3 tests/test_configuration.py
python3 scripts/build.py --diagnostics --profile Debug --test
```

`dependencies.lock.json` pins official Ubuntu 26.04 amd64 packages, including SDL 3.4.2, Vulkan headers and glslang, by version and SHA256. Downloads are verified before extraction; no sudo/system installation is needed. Persistent tools install under `/home/helena/dev/helworks/tools/linux-native`. The loader and NVIDIA driver are supplied by the system; the binary also dynamically uses SDL3. Normal package resolution supplies the loader; the local diagnostic build uses the explicit system loader and extracted package prefix.

Output always overwrites `/tmp/helengine/linux` on Linux and `C:\temp\helengine\linux` for managed verification on Windows. The primary artifact is `/tmp/helengine/linux/helengine_linux_gpu_test`. SPIR-V outputs are `/tmp/helengine/linux/shaders/quad.vert.spv` and `/tmp/helengine/linux/shaders/quad.frag.spv`.

Verified on NVIDIA GeForce GTX 1070, driver 580.178.04: real sampled-image quads, ordered alpha composition, nested clipping, target resize and zero-drawable deferral, checked by GPU readback. The diagnostic creates no window and captures no desktop image. The SDL window/swapchain path compiles but has not been executed under X11 or Wayland. Optional validation is explicit; requesting an absent Khronos validation layer fails contextually. Runtime Vulkan minimum is 1.1; no ray-tracing extension is requested. Submission currently uses fences and synchronous uploads/readback for correctness; throughput has not been measured.

## Pipeline interface

CMake accepts `HELENGINE_CORE_CPP_ROOT`, `HELENGINE_CODE_ROOT`, `HELENGINE_CONTENT_ROOT` and `HELENGINE_NATIVE_PROFILE=Debug|Release|Profiler`. A player build requires the generated `helcpp_config.hpp`; `HELENGINE_LINUX_BUILD_PLAYER=OFF` explicitly selects diagnostics. Profiler metadata is published for pipeline alignment, but real Tracy/player instrumentation remains pending with the core bridge. No profile parity is claimed from diagnostic builds.

The builder project is `C:\dev\helworks\helengine-linux\builder\helengine.linux.builder.csproj`; its assembly is `helengine.linux.builder.dll` and entry type is `helengine.linux.builder.LinuxPlatformAssetBuilder`. Set `HELENGINE_LINUX_REPOSITORY_ROOT` to the actual sibling checkout when loading the assembly outside its repository tree. Its native executor uses structured process arguments and preserves child failure and cancellation; full player requests currently fail on the incomplete core bridge.

```powershell
dotnet run --project C:\dev\helworks\helengine-linux\builder.tests --artifacts-path C:\temp\helengine\linux\managed
```

The generated 2D adapter will consume `RenderCommandListBuilder2D.Build(camera.RenderQueue2D)` in command order: `ClipPush`, `ClipPop`, `TexturedQuad`, `GlyphQuad` and `RoundedRect`. Quad geometry/pivots and source rectangles remain engine-owned. The low-level GPU ABI uses explicit float32 vertices while engine configurable arithmetic retains its chosen numeric policy.
