---
name: android-port-architecture
description: >-
  Use this skill when making ANY code change to the Dante's Inferno Android port.
  It enforces layered architecture discipline, prevents regressions, and guides
  the correct order of operations for CPU, memory, GPU/Vulkan, kernel, and
  Android platform work. Activate before writing, modifying, or reviewing code
  in the Android port to ensure changes follow the established engineering
  principles and do not introduce hacks across layer boundaries.
---

# Android Port Architecture — Engineering Discipline

This skill codifies the principles from the project's technical guide
([ReXGlue_Xenia_Android_ARM64_Vulkan_Turnip_Guia_Tecnico.md](file:///media/windroid/SSD%20KING/Dantes-inferno-Android/docs/ReXGlue_Xenia_Android_ARM64_Vulkan_Turnip_Guia_Tecnico.md))
into actionable rules the agent MUST follow.

---

## Golden Rule

> **Separate problems by layer. A bug in one layer must NOT be "fixed" with a
> hack in another layer.**

```text
Layer stack (top → bottom):
  Recompiled Game Code
  ReXGlue Runtime (Xbox kernel shims, memory, threads, APU)
  Xbox GPU Layer (registers, shaders, EDRAM, textures)
  Vulkan Backend (pipeline, descriptor, sync, command)
  Android Loader
  Mesa / Turnip
  KGSL / Kernel
  Adreno GPU
```

Before every change, identify **which layer** the problem lives in and make
the fix **in that layer only**.

---

## Pre-Change Checklist

Before writing ANY code change, verify:

1. **Which layer does this change belong to?** (CPU / Memory / GPU / Vulkan / Kernel / Android)
2. **Does this change cross layer boundaries?** → If yes, STOP and re-evaluate.
3. **Is there a measurement/benchmark proving this change is needed?** → If no, measure first.
4. **Does this change hardcode assumptions?** Check for:
   - `PAGE_SIZE = 4096` → use `sysconf(_SC_PAGESIZE)`
   - `ptr & ~0xFFF` → page-size-dependent, forbidden
   - Android version checks for Vulkan features → use `vkGetPhysicalDeviceFeatures2()`
   - SSE/AVX intrinsics → must be NEON or portable C++
   - `#define` endianness assumptions → use centralized `load_be*/store_be*` helpers
5. **Does this change affect precision?** (float, NaN, denormals, rounding, integer overflow)
   → Correctness before speed. Test thoroughly.

---

## Phase Order (Never Skip Phases)

Changes MUST follow this priority order. Do not optimize (Phase 6) before
the foundation (Phases 1–5) is solid.

| Phase | Focus                | Key Work                                      |
|-------|----------------------|-----------------------------------------------|
| 1     | Boot                 | APK → native lib → runtime → game loads       |
| 2     | CPU                  | PPC→ARM64 correctness, threading, ABI          |
| 3     | Memory               | Guest memory, host mappings, page-size safe    |
| 4     | Vulkan Init          | Device, queue, swapchain, basic draw           |
| 5     | Xbox GPU             | Registers, draw state, shaders, render targets |
| 6     | Optimization         | Cache, pipeline, sync, memory, profiling       |

---

## Vulkan Rules

- **Feature detection**: Always query `vkGetPhysicalDeviceFeatures2()`, never assume by Android version.
- **Pipeline cache**: Deterministic key = `game_id + shader_hash + translator_version + backend_version + GPU_family + driver_id`.
- **Descriptor management**: Persistent pools + ring allocators + recycling. Never create/destroy per-frame.
- **Memory allocation**: Sub-allocate from large blocks. Control alignment, memory type, coherency.
- **Command buffers**: Per-frame command pool, recycle buffers. Never destroy/create pools per frame.
- **Synchronization**: Minimize CPU↔GPU waits. Use timeline semaphores, `VK_KHR_synchronization2` when available. Never busy-wait.
- **Texture uploads**: Batch into single command buffer. Never submit per-texture.
- **Dynamic rendering**: Use `VK_KHR_dynamic_rendering` only when measured, keep renderpass fallback.

---

## Android Platform Rules

- **ABI**: `arm64-v8a` only. No `armeabi-v7a` without explicit justification.
- **Page size**: Support both 4 KB and 16 KB. Use `sysconf(_SC_PAGESIZE)`.
- **Storage**: Separate `save data` from `cache`. Cache can be cleared by OS.
- **JNI**: Minimize crossings. Main loop stays native. Thick interfaces only.
- **Register x18**: Reserved by Android platform. Never manipulate.
- **Lifecycle**: Handle `ANativeWindow` creation/destruction, surface recreation.
- **Build**: CMake + Ninja + Clang + Android NDK + Gradle. Separate CMake targets per module.

---

## Stutter Classification

Before fixing stutter, classify it:

| Type | Cause                  | Indicator                          |
|------|------------------------|------------------------------------|
| A    | Shader compilation     | CPU spike during first-time draw   |
| B    | Pipeline creation      | `vkCreateGraphicsPipelines` spike  |
| C    | GPU starvation         | GPU idle, CPU late                 |
| D    | CPU bottleneck         | CPU 100%, GPU underutilized        |
| E    | Synchronization        | CPU waiting on GPU unnecessarily   |
| F    | Thermal throttling     | Temperature ↑, frequency ↓, FPS ↓ |

Each type requires a different fix. Never apply a generic "optimization".

---

## Benchmark Requirements

Never use FPS alone. Always measure:

```text
CPU frame time, GPU frame time, p50/p95/p99 frame times,
shader compilation time, pipeline creation time,
queue submit count, command buffer count,
memory usage, GPU/CPU frequency, thermal state
```

Run sustained benchmarks: 5 min, 10 min, 20 min. 30-second benchmarks are misleading on mobile.

---

## Diagnostics (Log at Startup)

Every run must log:

```text
GPU family, Driver name/version, Mesa version, Vulkan API version,
Android version, Kernel version, Page size, KGSL info,
Supported extensions, Supported features, Memory heaps/types
```

---

## What NOT To Do

- ❌ Assume Vulkan 1.3 just because Android 15
- ❌ Assume Turnip eliminates all driver problems
- ❌ Assume new Android = new kernel
- ❌ Hardcode page size
- ❌ Hardcode memory addresses
- ❌ Block CPU waiting on GPU without necessity
- ❌ Create pipeline per draw call
- ❌ Compile same shader repeatedly
- ❌ Create thousands of threads
- ❌ Modify kernel without benchmark evidence
- ❌ Mix save data with cache
- ❌ Apply precision hacks without tests
- ❌ Use `-ffast-math` without IEEE compliance testing
- ❌ Optimize before measuring (NEON, CPU affinity, threading)
- ❌ Fix a Vulkan problem with a game-code hack (or vice versa)

---

## References

For full technical details, see:
- [Architecture details](./references/architecture_details.md) — Complete layer-by-layer technical reference
- [Production checklist](./references/production_checklist.md) — Full checklist for production readiness
- [Original technical guide](file:///media/windroid/SSD%20KING/Dantes-inferno-Android/docs/ReXGlue_Xenia_Android_ARM64_Vulkan_Turnip_Guia_Tecnico.md) — Source document
