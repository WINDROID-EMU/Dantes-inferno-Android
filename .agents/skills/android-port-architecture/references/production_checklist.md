# Production Checklist — Android ARM64 Port

Use this checklist to verify production readiness of each layer.

---

## CPU

- [ ] ARM64 pure (`arm64-v8a` only)
- [ ] No SSE/AVX dependencies
- [ ] NEON only in measured hotspots
- [ ] ABI correct (64-bit pointers, `long` = 8 bytes)
- [ ] Endianness correct (centralized `load_be*/store_be*` helpers)
- [ ] Alignment correct (no unaligned native loads assumed)
- [ ] Register `x18` not manipulated

## Memory

- [ ] Guest memory isolated from host
- [ ] Host mappings correct
- [ ] No `PAGE_SIZE` hardcoded (`sysconf(_SC_PAGESIZE)` used)
- [ ] Tested on 4 KB page devices
- [ ] Tested on 16 KB page devices
- [ ] ELF segment alignment verified for all `.so` files
- [ ] Memory allocator efficient (sub-allocation from large blocks)

## Vulkan

- [ ] Feature detection via `vkGetPhysicalDeviceFeatures2()`
- [ ] Extension detection (never assume by Android version)
- [ ] Pipeline cache with deterministic versioned keys
- [ ] Shader cache separate from pipeline cache
- [ ] Descriptor pool recycling (no per-frame create/destroy)
- [ ] Command pool recycling (per-frame pool, recyclable buffers)
- [ ] Barriers correct (validated with Vulkan validation layers)
- [ ] Timeline semaphores used when available
- [ ] `VK_KHR_synchronization2` used when available
- [ ] No busy-wait synchronization
- [ ] Texture uploads batched
- [ ] Staging buffer reuse
- [ ] Dynamic rendering with renderpass fallback

## Android

- [ ] `arm64-v8a` ABI target
- [ ] NDK up to date
- [ ] Native libraries 16 KB compatible
- [ ] Scoped storage (save ≠ cache)
- [ ] Lifecycle correct (Activity, Surface creation/destruction)
- [ ] `ANativeWindow` management
- [ ] Surface recreation handled
- [ ] JNI crossings minimized
- [ ] Main loop on native side

## GPU / Xbox 360

- [ ] EDRAM emulation correct
- [ ] Render target resolve working
- [ ] Shader translation deterministic
- [ ] MSAA handling correct
- [ ] Depth/stencil correct
- [ ] Blend operations correct
- [ ] Fragment shader interlock checked (not forced)

## Adreno / Turnip / Mesa

- [ ] Adreno GPU model identified and logged
- [ ] Turnip version identified and logged
- [ ] Mesa version identified and logged
- [ ] Vulkan API version logged
- [ ] All supported extensions logged
- [ ] KGSL version logged

## Kernel

- [ ] Kernel version logged
- [ ] Page size logged
- [ ] KGSL interface identified
- [ ] Futex capabilities detected (futex2/waitv fallback)
- [ ] Memory mapping verified
- [ ] GPU scheduler behavior understood
- [ ] Thermal governor behavior documented

## Benchmarking

- [ ] CPU frame time measured
- [ ] GPU frame time measured
- [ ] p50/p95/p99 frame times tracked
- [ ] Shader compilation time measured
- [ ] Pipeline creation time measured
- [ ] Queue submit count tracked
- [ ] Memory usage tracked
- [ ] GPU/CPU frequency logged
- [ ] Thermal state logged
- [ ] Sustained tests: 5 min, 10 min, 20 min
- [ ] Full device/driver/build info recorded with each benchmark

## Diagnostics (Startup Log)

- [ ] GPU family
- [ ] Driver name and version
- [ ] Mesa version
- [ ] Vulkan API version
- [ ] Android version
- [ ] Kernel version
- [ ] Page size
- [ ] KGSL info
- [ ] Supported extensions
- [ ] Supported features
- [ ] Memory heaps and types
