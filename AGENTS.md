# AGENTS.md - Project guide for AI agents

## Project

Static recompilation port of **Dante's Inferno** (Xbox 360) to native PC using
the ReXGlue SDK (v0.10.0). ReXGlue translates PowerPC XEX -> C++ ahead of time.

## Key paths

- `dantes_inferno_manifest.toml` - ReXGlue project manifest (SDK-managed; regen with `rexglue init --force`)
- `generated/rexglue.cmake` - SDK build boilerplate (auto-generated, DO NOT EDIT)
- `generated/default/` - codegen output (gitignored, produced during build)
- `src/dantes_inferno_app.h` - **user-owned** app class; override ReXApp hooks here
- `src/main.cpp` - entry point (SDK-managed, preserved on first init only)
- `game/` - extracted Xbox 360 game files (gitignored, copyrighted - never commit)
- `thirdparty/rexglue-sdk/` - SDK clone (gitignored, via setup.ps1)
- `docs/rexglue_notes.md` - ReXGlue workflow & command reference
- `docs/ultrawide_research.md` - Ultrawide support RE findings & implementation plan
- `tools/asset_tool.py` - asset extraction/packing tool (BIG/STR/TG4D/VP6)
- `tools/ASSET_TOOL_README.md` - asset tool documentation
- `tools/Gibbed.Visceral/` - format reference source (gitignored, Zlib license)
- `patches/` - local patches for SDK and generated code (tracked in git)
  - `patches/sdk/rexglue-sdk-v0.10.0.patch` - all SDK modifications
  - `patches/apply_sdk_patches.ps1` - applies SDK patches after clone
  - `patches/generated/apply_generated_patches.py` - applies fiber/setjmp/longjmp
    edits to generated code after codegen (must be re-run after each regen)

## Build commands (Windows)

```powershell
# One-time: build the rexglue CLI from the SDK
cmake --preset win-amd64-release -DREXSDK_DIR=thirdparty\rexglue-sdk
cmake --build out\build\win-amd64-release --target rexglue

# Regenerate SDK-managed files (requires game/default.xex present)
rexglue init --force --project_name dantes_inferno --project_root . --xex_path game\default.xex --game_root game

# Build the port (codegen runs automatically as a build dependency)
cmake --preset win-amd64-release -DREXSDK_DIR=thirdparty\rexglue-sdk
cmake --build out\build\win-amd64-release
```

Run: `out\win-amd64\Release\dantes_inferno.exe`

## Toolchain

- Clang 18+ required (NOT MSVC/GCC). Detected: Clang 22 at `C:\Program Files\LLVM\bin\clang.exe`
- CMake 3.25+, Ninja, Visual Studio 2022 (Windows SDK for D3D12)
- C++23, D3D12 graphics backend on Windows

## Conventions

- `src/dantes_inferno_app.h` is the ONLY place for custom app behavior. Do not
  edit `main.cpp` or `generated/rexglue.cmake` - they are SDK-managed and get
  overwritten by `rexglue init`/`rexglue migrate`.
- For per-instruction custom C++ injection, use `[[mid_asm_hooks]]` in the
  manifest (see docs/rexglue_notes.md).
- Game assets under `game/` are copyrighted and gitignored. Never commit them.
- The SDK under `thirdparty/rexglue-sdk/` is gitignored; re-clone via `setup.ps1`.

## Naming

Project name `dantes_inferno` -> snake_case `dantes_inferno`, PascalCase
`DantesInferno`, UPPER `DANTESINFERNO`. CMake target: `dantes_inferno`.

## Improvement plan

See `docs/improvements_plan.md` for full research findings. Summary:

1. **Graphics quality** (Phase 1, no RE): resolution_scale, anisotropic_override,
   swap_post_effect=fxaa cvars in OnPreSetup. Optionally FidelityFX FSR build.
2. **Input config** (Phase 2, no RE): SDL backend + MnK keybind defaults in
   OnPreSetup. DualShock/DualSense/Xbox all supported via SDL3.
3. **DLC auto-install** (Phase 3, no RE): OnPostSetup hook scans dlc/ folder,
   calls ContentManager::InstallContent() on each STFS package.
4. **Ultrawide** (Phase 4, requires RE): midasm_hook on projection matrix to
   patch aspect ratio. Hor+ anamorphic render strategy.
5. **Button glyphs** (Phase 5, requires RE): replace game's button prompt
   textures based on active input device. Needs SDK patch for device detection
   or glyph_family cvar. Glyph art in metadata/glyphs/.
6. **Installer** (Phase 6): asks user for ISO + DLC folder, extracts to game/
   and dlc/. No STFS logic in installer.

~~Known blocker: ReXGlue issue #75 — Dante's Inferno crashes at startup due to
unimplemented VMX/Altivec PPC instructions (v0.1.1).~~ **Resolved in v0.10.0.**
Game boots and runs. VMX builder bugs causing FMV corruption were found and
fixed (see `docs/vp6_fmv_corruption_fix.md`).

## SDK patches

The SDK under `thirdparty/rexglue-sdk/` has local patches to
`src/codegen/builders/vector.cpp` that fix VMX instruction builder bugs.
Full technical documentation: `docs/vp6_fmv_corruption_fix.md`.

If the SDK is re-cloned, these patches must be re-applied. The fixes are:

1. **`vpkuwus` / `vpkuhus` in-place aliasing** (root cause of FMV corruption):
   Element-by-element packing loops aliased the destination's narrowed array
   with the source's wider array. Replaced with SSE intrinsics.
2. **`vmsum3fp128` dot product mask** (`0x7F` → `0xEF`): A previous "fix"
   incorrectly changed the mask from `0xEF` to `0x7F`, which excluded PPC
   element 0 (X component) from 3-element dot products instead of excluding
   PPC element 3 (W). This broke physics/collision code, causing the
   character to fall through the map after the opening cutscene. Reverted
   to `0xEF` after the `ppc_tests` test suite caught the regression.
3. **Pack builder `unpackhi_epi64` removal**: Pack builders discarded half
   the packed elements via `unpackhi_epi64`. Removed and operand-swapped for
   byte reversal.

## Build & run notes

- The executable loads `rexgpu-xenos.dll` from its own directory, not from
  the SDK output. After rebuilding the SDK, copy:
  `thirdparty\rexglue-sdk\out\win-amd64\rexgpu-xenos.dll` →
  `out\build\win-amd64-release\rexgpu-xenos.dll`
- Always launch with `--game_data_root=game` from the project root.
- Runtime logs are in `out\build\win-amd64-release\logs\`.
- After changing SDK codegen builders, delete the stale generated files to
  force regeneration. The codegen stamp does NOT track `rexglue.exe` as a
  dependency, so you must delete the stamps AND the generated files, then
  reconfigure CMake and rebuild:
  ```powershell
  Remove-Item generated\default\codegen.* -Force
  Remove-Item generated\default\dantes_inferno_recomp.*.cpp -Force
  Remove-Item generated\default\dantes_inferno_recomp.*.h -Force
  Remove-Item generated\default\sources.cmake -Force
  cmake --preset win-amd64-release -DREXSDK_DIR=thirdparty\rexglue-sdk
  cmake --build out\build\win-amd64-release
  python patches\generated\apply_generated_patches.py
  ```
- The SDK's PPC instruction test suite (`ppc_tests`) can be built and run
  to verify codegen builder correctness:
  ```powershell
  cd thirdparty\rexglue-sdk
  cmake --preset win-amd64 -DREXGLUE_BUILD_TESTS=ON
  cmake --build out\build\win-amd64 --target ppc_tests --config Release
  .\out\win-amd64\Release\ppc_tests.exe
  ```

## Save system

The save system uses a setjmp/longjmp pair that requires C setjmp/longjmp
to unwind the C++ call stack. See `src/dantes_inferno_hooks.h` for the
fiber support functions and `docs/code_changes.md` for full details.

Key components:
- `[[midasm_hook]]` in manifest injects `ZeroFiberSwitchCallback()` at
  `sub_82701240` (guest setjmp)
- `OnPreLaunchModule` zeroes the fiber-switch callback at `0x82B101E4`
- Manual generated-code edits in `.24`, `.38`, `.45`, `.70` add C
  setjmp/longjmp calls (lost on codegen regen, must be re-applied)
- `XUserFindUsers` handler in `xlivebase_app.cpp` returns success to
  prevent null-pointer crash when loading saves

## Asset extraction tool

`tools/asset_tool.py` extracts and repacks game assets for upscaling. See
`tools/ASSET_TOOL_README.md` for full documentation.

```powershell
# List archive contents
python tools/asset_tool.py list game/bigfile0.viv

# Full pipeline: extract + unpack STR + convert textures to PNG
python tools/asset_tool.py extract game/bigfile0.viv output --full-pipeline

# Extract just videos
python tools/asset_tool.py extract game/bigfile0.viv output --type videos

# Unpack/repack STR files
python tools/asset_tool.py unpack-str input.str output_dir/
python tools/asset_tool.py pack-str input_dir/ output.str

# Convert textures (TG4D <-> DDS/PNG)
python tools/asset_tool.py convert-texture tex.tg4d tex.png --tg4h tex.tg4h
python tools/asset_tool.py make-texture upscaled.png out.tg4d --format dxt5

# Pack BIG archive
python tools/asset_tool.py pack-big input_dir/ output.viv
```

Dependencies: `pip install pillow texture2ddecoder`

Formats supported: BIG/VIV (BIGH), STR (StreamSet), TG4D/TG4H (DXT1/DXT5),
VP6 video, EAGM mesh (raw extraction). RefPack compression/decompression
implemented in pure Python.

## Current status

- [x] SDK cloned at v0.10.0
- [x] Project scaffolding created from ReXGlue v0.10.0 init templates
- [x] GitHub repo created: https://github.com/florinp93/dantes-inferno (private)
- [x] Game ISO + DLC file placed in disc/ (ISO 7.8GB, DLC is STFS LIVE package)
- [x] Improvement research completed (docs/improvements_plan.md)
- [x] SDK submodules initialized & CLI built
- [x] Game ISO extracted into `game/` with `default.xex` entrypoint
- [x] `rexglue init --force` run to stamp SDK-managed files
- [x] First successful codegen + build
- [x] VMX/AltiVec issue #75 resolved (v0.10.0 has full VMX support)
- [x] VP6/Bink FMV corruption diagnosed and fixed (docs/vp6_fmv_corruption_fix.md)
- [x] SDK patches submitted upstream (PR #426)
- [x] Physics/collision bug fixed: vmsum3fp128 dot product mask reverted
      from 0x7F to 0xEF (character was falling through map after cutscene)
- [x] Save system fixed: fiber/setjmp/longjmp support, midasm_hook,
      XUserFindUsers handler, OnPreLaunchModule patch
- [x] Asset extraction tool built (tools/asset_tool.py): BIG/VIV parsing,
      STR unpacking/packing, RefPack, TG4D/DXT texture conversion, VP6
      extraction, EAGM mesh extraction
- [x] Graphics quality cvars configured in OnPreSetup
- [x] MnK keybind defaults configured in OnPreSetup
- [x] Android ARM64 port implemented (Vulkan + SDL3 + AdrenoTools Turnip)
- [x] In-engine ISO installer (dantes_iso_installer) extracts XDVDFS directly on Android
- [x] 114 recompilation C++ units tracked in `generated/default/` with setjmp patches applied
- [x] Android stability fixes (landscape lock, Scudo teardown fix, user_data permissions, WindowInsetsController null-safety on Android 11+ / Moto G100)
- [x] Settings screen overhaul (responsive 2-column landscape layout, high-DPI stacked fields, full persistence via SharedPreferences, dantes_inferno.toml, and JNI cvars bridge)
- [x] VP6 FMV green artifacts fix: patched `vpkuwus128` in `recomp.35.cpp` and `recomp.103.cpp` with atomic SIMDE pack intrinsics, integrated into `apply_generated_patches.py`
- [x] Audio stuttering fix: disabled low latency audio mode (`SDL_ANDROID_LOW_LATENCY_AUDIO=0`), enlarged sample buffer to 2048 frames (`SDL_AUDIO_DEVICE_SAMPLE_FRAMES=2048`), and raised `audio_maxqframes=128`
- [x] ARM64 CPU Emulation performance optimization:
      - `ignore_thread_affinities = true` & `ignore_thread_priorities = true`: unpinned guest threads from host cores 0..3 (LITTLE low-power A510 cores) to unleash full power of Big (Cortex-A710) and Prime (Cortex-X2) cores on Snapdragon SoCs.
      - Native ARM64 NEON vector conversions (`vmaxq_f32`, `vcvtq_u32_f32`, `vcvtq_s32_f32`, `vcvtq_f32_u32`) replacing slow simulated multi-step conversions in `thirdparty/rexglue-sdk/include/rex/ppc/intrinsics.h`.
      - ReXGlue SDK AltiVec NEON engine acceleration (`thirdparty/rexglue-sdk/include/rex/ppc/intrinsics.h`):
        * `simde_mm_perm_epi8_` (PPC `vperm`): eliminated emulated `pshufb`/`blendv`/`slli` in favor of direct NEON 2-vector register table lookup (`vqtbl1q_u8` + `vtstq_u8` + `vbslq_u8`).
        * `simde_mm_adds_epu32`: single-cycle hardware `vqaddq_u32` (UQADD) replacing 4 emulated steps.
        * `simde_mm_cmpgt_epu8` & `simde_mm_cmpgt_epu16`: hardware unsigned compares `vcgtq_u8` and `vcgtq_u16` (UCMGT) eliminating sign-bit bias XOR operations.
        * `simde_mm_avg_*`: native hardware rounding halving adds (`vrhaddq_s8`, `vrhaddq_s16`, `vrhaddq_s32`).
        * `simde_mm_vslo` & `simde_mm_vsro`: zero-memory-copy register octet shifts using `vextq_u8`, eliminating stack memory spills and `memcpy`.
        * `simde_mm_vsl`: eliminated stack roundtrip memory stores and reloads with direct register `vshlq_u64`.
        * `simde_mm_s*v_epi*`: native per-lane variable shifts `vshlq_u16`, `vshlq_s16`, `vshlq_u8` replacing multi-step unpack/shift/pack sequences.
        * `vec128.h`: replaced scalar broadcast initialization loops (`vec128i`, `vec128q`, `vec128f`, `vec128s`, `vec128b`) with native hardware `vdupq_n_*` instructions.
        * `thread.h`: optimized `Fence::Signal()` with wait-count check to bypass redundant `cond_.notify_all()` kernel futex syscalls when no threads are waiting.
      - Clang compiler optimization flags `-O3 -fomit-frame-pointer -fno-stack-protector -ffp-contract=fast -fvectorize` removing overhead across 20,000+ recompiled guest functions.
      - Fixed Turnip crash recovery false-positive in `MainActivity.java` so high-performance Mesa driver remains active.
- [x] In-game real-time FPS & Frametime (ms) HUD overlay with toggle in Settings, color-coded performance indicators (green/yellow/red), and isolated alpha/visibility.
- [x] Genuine Xbox 360 guest emulation FPS measurement: hooked directly into `VdSwap` (`0x827CEE14`), with stall detection for lag/compilation pauses, and removed deceptive Android `Choreographer` display rate fallback.
- [x] Vulkan pipeline & shader stutter optimization: enabled persistent on-disk Mesa shader cache (`MESA_SHADER_CACHE_DIR`, `MESA_DISK_CACHE_SINGLE_FILE=1`), `vulkan_async_skip_incomplete_frames=true`, and increased compilation threads to 6.
- [x] Enhanced audio underrun prevention: expanded SDL audio device sample buffer to 4096 frames (~85ms safety buffer) and `audio_maxqframes=256`.
- [x] Prebuilt release APK packaged at `apk/dantes_inferno_arm64.apk`
- [x] Automated GitHub Actions CI/CD workflow (`.github/workflows/android-release.yml`): builds ARM64 release APK and automatically publishes GitHub Releases on tag, push to main, or workflow dispatch.
- [x] Qualcomm Adreno Vulkan crash fix: disabled `vulkan_sparse_shared_memory`, `vulkan_push_constants_descriptors`, and `vulkan_deferred_resolve_clears` on Android to eliminate fatal driver aborts on Adreno 730/740/830.
- [x] 16 KB Page Size support (Android 15+): added `-Wl,-z,max-page-size=16384` linker flags across CMake targets and Gradle build script with automated ELF alignment CI verification.
- [x] In-app GitHub release updater (`AppUpdater`): asynchronous update checking against GitHub Releases API, background progress download, and direct APK installation via `FileProvider`.
- [x] Instant Zero-Copy ISO attachment: using `detachFd()` and `/proc/self/fd/<fd>` in `TitleActivity.java`, eliminating the 7.8 GB storage copy and allowing instantaneous game launching/extraction.
- [x] 60 Hz Display Mode locking & ALLM: pinned display refresh rate to 60.0 Hz via `selectSixtyHertzDisplayMode()` to eliminate cadence judder on 90Hz/120Hz/144Hz displays, plus minimal post-processing (ALLM / Game Mode).
- [x] Isolated Process Restart (`RestartActivity`): clean out-of-process relaunch (`android:process=":restart"`) ensuring complete termination of Vulkan/AdrenoTools driver state when switching graphics configurations.
- [x] Compositor-level Performance Benchmark (`tools/bench.sh`): SurfaceFlinger frame present interval analysis, frametime p50/p95/p99, CPU %, PSS memory, and thermal state monitoring over ADB.
- [x] Native Android AAudio Integration via SDL3:
      - Configured SDL3 to use Android's native AAudio backend (`SDL_HINT_AUDIO_DRIVER="AAudio"`) with `AAUDIO_USAGE_GAME` / `AAUDIO_CONTENT_TYPE_GAME` DSP routing (`SDL_HINT_AUDIO_DEVICE_STREAM_ROLE="Game"`).
      - Preserves Android Java audio focus (`AUDIOFOCUS_GAIN`), audio device routing, and lifecycle state management.
      - Integrated with SDK `SDLAudioDriver`'s producer-consumer semaphore pacing and 4096-frame (~85ms) underrun protection buffer.
- [x] ARM64 Architecture Overhead & Thermal Wear Relief (`thirdparty/rexglue-sdk/include/rex`):
      - `simde_mm_dp_ps` specialization for mask `0xEF` (PPC `vmsum3fp128`): eliminated stack memory arrays, writes, and reload latency across 1,958 3D vector functions in favor of 2-instruction pure NEON register dot products (`vsetq_lane_f32` + `vdupq_n_f32(vaddvq_f32)`).
      - `CRRegister::setFromMask`: replaced slow emulated `simde_mm_movemask_ps` (~12 instructions) with single-cycle native ARM64 vector reduction instructions (`vmaxvq_s32` / `vminvq_s32`) on critical PPC AltiVec branching paths.
      - Hardware `YIELD` instruction (`rex::platform::CpuYield()` / `rex::thread::MaybeYield()`): deschedules CPU speculative execution during spinlocks, thread waits, and sync points to prevent runaway thermal throttling on ARM64 big/prime cores.
      - Inlined bit manipulation (`math.h`): inlined `lzcnt`, `tzcnt`, `bit_scan_forward`, and `rotate_left` using standard C++20 `<bit>` (`std::countl_zero`, `std::countr_zero`, `std::rotl`) mapping directly to native `CLZ` and `ROR` hardware instructions.
      - 64-bit register safety in `FPSCRPlatform::setcsr` (`msr fpcr`): ensured strict 64-bit register allocation conforming to AArch64 ABI.
- [x] Ultrawide projection hook (ported from upstream v0.5.0 / commits ab67ab0e + d6187367):
      - `UltrawideAspectHook` precision fix: changed from `f29.f32` to `f29.f64` — reading the
        lower 32 bits of a PPC double register as float produced denormal values, silently
        preventing the hook from ever firing (upstream bug confirmed). Now correctly uses 64-bit.
      - New `UltrawideXScaleHook(f12)` at `0x8251DD38`: scales the X column of the projection
        matrix by `kNativeAspect / target_aspect`, preventing horizontal geometry distortion on
        non-16:9 displays. Registered in `dantes_inferno_manifest.toml` as second `[[midasm_hook]]`.
      - `kNativeAspect = 1.7777778` constant for consistent 16:9 baseline across both hooks.
      - Upstream v0.5.0 Native Vulkan Renderer (DiligentCore + D3D12 interop) analysed and
        confirmed incompatible with Android: requires `VK_KHR_external_memory_win32` and
        `ID3D12Resource::CreateSharedHandle` — Win32-only APIs with no Android equivalent.
        Android port already uses native Vulkan (Turnip/Mesa) end-to-end without D3D12.
- [ ] DLC auto-install hook in OnPostSetup
- [ ] Button glyph replacement (requires RE of generated code)

