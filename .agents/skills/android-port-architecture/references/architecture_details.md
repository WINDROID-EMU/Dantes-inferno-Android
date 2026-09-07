# Architecture Details — Android ARM64 Port Reference

This is the full technical reference for each layer of the Android port
architecture. Read this when you need deeper context on a specific layer.

---

## 1. CPU Layer: PowerPC → ARM64

### ReXGlue vs Xenia

- **Xenia** = emulator with JIT (PPC → host at runtime)
- **ReXGlue** = static/AOT recompilation (PPC → C++ → Clang → ARM64 native)

ReXGlue removes the cost of dynamic instruction translation but does NOT
eliminate the cost of the Xbox 360 runtime (kernel, memory, GPU, audio, APIs).

### Endianness

- Xbox 360 PowerPC = big-endian
- ARM64 Android = little-endian
- Use centralized helpers: `load_be16/32/64()`, `store_be16/32/64()`
- Never scatter manual byte-swapping throughout the codebase

### Alignment

- Never assume any address can be accessed with native ARM64 loads
- Use safe access patterns when guest allows unaligned addresses

### ABI Gotchas on ARM64 Android

- Pointers are 64-bit, not 32-bit — beware `uint32_t` casts
- `sizeof(long)` is 8, not 4
- Register `x18` is platform-reserved — never manipulate
- No SSE/AVX — use NEON only after benchmarking hotspots

### AOT Compilation Best Practices

- Use `constexpr` when possible
- Keep functions small and predictable
- Avoid unnecessary indirection
- Preserve correct aliasing
- Avoid `volatile` without necessity
- Use `restrict` only when semantically correct
- Use NEON intrinsics only after measurement
- Keep hot paths isolated

---

## 2. Memory Layer

### Guest vs Host Memory

```text
Xbox 360 guest virtual memory
        ↓
Host virtual memory (mmap)
        ↓
Physical RAM (shared with GPU on UMA/Adreno)
```

### Page Size

- Historical Android: 4 KB pages
- Android 15+: AOSP supports 16 KB pages on compatible ARM64
- **NEVER** use `#define PAGE_SIZE 4096` or `ptr & ~0xFFF`
- **ALWAYS** use `sysconf(_SC_PAGESIZE)` or equivalent

### ELF Alignment

- All native libraries (`.so`) must be built for both 4 KB and 16 KB alignment
- Verify each library individually when targeting 16 KB devices

### GPU Memory (UMA Architecture)

- Adreno uses Unified Memory Architecture (UMA)
- CPU and GPU share physical memory
- Different caches, mappings, and memory properties
- Important: cache coherency, memory domains, buffer usage, alignment, sync

---

## 3. GPU Layer: Xbox 360 GPU Emulation

### EDRAM

The Xbox 360 GPU has an EDRAM architecture that cannot be simply mapped to a
standard Vulkan texture.

```text
Xbox 360 render target → EDRAM model → resolve → Vulkan image
```

Must preserve: format, MSAA, depth/stencil, resolve, tiling, blend ops,
read/write ordering, synchronization.

### ROV / Fragment Shader Interlock

- Some Xbox 360 rendering patterns require ordered fragment access
- Check `VkPhysicalDeviceFeatures` for fragment shader interlock support
- Never force an extension that doesn't exist on the device

### Shader Translation

```text
Guest Xbox shader → IR → SPIR-V → VkShaderModule → VkPipeline
```

- Translation must be deterministic
- Same guest shader hash → same SPIR-V output (enables caching)

---

## 4. Vulkan Layer

### Pipeline Flow

```text
Guest GPU state → state translator → shader translator → SPIR-V
→ VkShaderModule → pipeline state → VkPipeline → command buffer
→ queue submission → Turnip → Adreno
```

### Pipeline Cache Strategy

Key = `game_id + shader_hash + translator_version + backend_version + GPU_family + driver_id`

Requirements:
- Deterministic key
- Versioning
- GPU/driver identification
- Safe invalidation
- Atomic writes
- Corruption tolerance
- Per-backend-version cache

Never reuse cache blindly from another driver.

### Shader Cache (Separate from Pipeline Cache)

```text
Guest shader → Translated IR → SPIR-V → Driver compilation → GPU executable
```

Cache the stable, reproducible parts.

### Descriptor Management

- Persistent descriptor pools
- Ring allocators
- Recycling
- Never create/destroy descriptor sets continuously
- Verify feature bits before using descriptor indexing

### Memory Allocation

- Sub-allocate from large GPU allocations
- Never one `VkDeviceMemory` per small resource
- Control: alignment, memory type, host-visible, device-local, coherent, cached, transient

### Command Buffers

- Per-frame command pool + recyclable command buffers
- Never destroy/create pools every frame

### Synchronization

```text
Good: CPU producer → command buffer → GPU queue → fence/timeline → CPU continues
Bad:  CPU → GPU submit → wait → CPU → GPU (serial)
```

- Use atomics, futex, condition variables, semaphores
- Use Vulkan timeline semaphores and `VK_KHR_synchronization2`
- Never busy-wait: `while (!done) {}` wastes CPU and battery

### Dynamic Rendering

- Use `VK_KHR_dynamic_rendering` only when benchmarks show benefit
- Always keep render pass fallback

### Texture Uploads

```text
Bad:  upload → submit → upload → submit → upload → submit
Good: batch uploads → single command buffer → single/few submissions
```

### Staging Buffers

```text
CPU → staging buffer → GPU copy → device-local resource
```

Don't call `vkMapMemory`/`vkUnmapMemory` for every small transfer.

---

## 5. Android Platform Layer

### Vulkan Chain

```text
Application → libvulkan.so → Android Vulkan Loader → Vulkan Driver → Kernel GPU driver → GPU
```

### Turnip (Mesa Vulkan for Adreno)

```text
Vulkan API → Turnip → Freedreno → KGSL/kernel → Adreno
```

**Turnip replaces user-space Vulkan implementation but does NOT replace the
kernel GPU driver.** Old kernel + new Turnip ≠ new kernel + new Turnip.

### KGSL (Qualcomm kernel GPU interface)

Controls: GPU access, memory, MMU/IOMMU, command submission, synchronization,
fences, GPU scheduling, power management.

### SurfaceFlinger

```text
Vulkan → ANativeWindow → BufferQueue → SurfaceFlinger → Hardware Composer → Display
```

Avoid unnecessary copies and excessive synchronization with presentation.

### Frame Pacing

Coordinate: game timing + GPU completion + display vsync.
Don't block CPU on every frame.

### Audio

```text
Good: Game → audio ring buffer → audio thread → Android audio API
Bad:  render frame → mix audio → wait audio → next frame
```

Audio must be decoupled from the frame loop.

### Filesystem / Storage

- Internal app data: persistent, private
- External app-specific storage: user-accessible
- Cache: can be cleared by OS at any time
- Save data: must never be in cache directory
- Shader cache / pipeline cache: separate from save data

### JNI Bridge

- Main loop stays native
- Minimize JNI crossings
- Use thick interfaces (batch data, not per-object calls)

### Build System

```text
CMake + Ninja + Clang + Android NDK + Gradle
```

- Separate CMake targets: `rexruntime`, `rexgpu`, `rexvulkan`, `game`
- Use `INTERFACE`/`PRIVATE`/`PUBLIC` dependency declarations
- Target `arm64-v8a` only (no `armeabi-v7a` without justification)

### Compiler Flags

- Start with `-O2`, only use `-O3` after benchmarking
- Never use `-ffast-math` without IEEE compliance testing
- For emulation, precision > micro-optimization

---

## 6. Kernel Layer

### Kernel Variability

Android version ≠ kernel version. Same Android 15 can ship with kernel 5.10
or kernel 6.1 — they behave differently.

### Futex

- `futex2` introduces `futex_waitv()` for waiting on multiple futexes
- Always do capability detection — not all kernels expose same futex interface
- Never assume kernel features based on Android version

### Kernel Customization (Last Resort)

Order: fix ReXGlue → fix Vulkan backend → fix Turnip → THEN consider kernel.

Only when benchmarks prove kernel is the bottleneck. Areas:
scheduler, futex, memory mapping, KGSL, GPU frequency, thermal, power mgmt.

---

## 7. Thermal Management

```text
Performance → Temperature ↑ → Thermal governor → Frequency ↓ → FPS ↓
```

- 30-second benchmarks are misleading
- Test at 5 min, 10 min, 20 min for sustained performance
- Record GPU busy, GPU frequency, GPU temperature, frame time — together

---

## 8. Diagnostics & Profiling Tools

### Android
- `adb`, `logcat`, `perfetto`, `simpleperf`, `dumpsys`

### Vulkan
- Validation layers, RenderDoc (when compatible), loader diagnostics, GPU vendor tools

### Mesa
- `MESA_DEBUG`, shader/cache diagnostics, Turnip/Freedreno debug facilities

### Linux/Desktop
- `perf`, `gdb`, `lldb`, `strace`

### Log Categories
`CPU`, `GPU`, `VULKAN`, `SHADER`, `PIPELINE`, `MEMORY`, `KERNEL`, `AUDIO`, `INPUT`, `ANDROID`

### Log Levels
`ERROR`, `WARN`, `INFO`, `DEBUG`, `TRACE`

Never leave `TRACE` enabled during benchmarks.
