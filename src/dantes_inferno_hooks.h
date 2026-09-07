#pragma once

#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/logging/macros.h>
#include <cstdint>
#include <csetjmp>
#include <atomic>
#include <chrono>

// Real-time guest engine statistics (hooked directly into VdSwap 0x827CEE14)
inline std::atomic<float> g_guest_fps{0.0f};
inline std::atomic<float> g_guest_frametime_ms{0.0f};
inline std::atomic<uint64_t> g_guest_frame_count{0};
inline std::chrono::steady_clock::time_point g_last_swap_time;
inline double g_smoothed_guest_fps = 0.0;
inline double g_smoothed_guest_ft = 0.0;

inline void OnGuestVdSwap() {
  auto now = std::chrono::steady_clock::now();
  g_guest_frame_count.fetch_add(1, std::memory_order_relaxed);

  if (g_last_swap_time.time_since_epoch().count() > 0) {
    double delta_ms = std::chrono::duration<double, std::milli>(now - g_last_swap_time).count();
    if (delta_ms > 0.0) {
      double instant_fps = 1000.0 / delta_ms;
      if (g_smoothed_guest_fps <= 0.0) {
        g_smoothed_guest_fps = instant_fps;
        g_smoothed_guest_ft = delta_ms;
      } else {
        // Fast response with smooth stabilization
        g_smoothed_guest_fps = g_smoothed_guest_fps * 0.80 + instant_fps * 0.20;
        g_smoothed_guest_ft = g_smoothed_guest_ft * 0.80 + delta_ms * 0.20;
      }
      g_guest_fps.store(static_cast<float>(g_smoothed_guest_fps), std::memory_order_relaxed);
      g_guest_frametime_ms.store(static_cast<float>(g_smoothed_guest_ft), std::memory_order_relaxed);
    }
  }
  g_last_swap_time = now;
}

inline thread_local jmp_buf g_fiber_jmp_buf;
inline thread_local uint32_t g_setjmp_ctx_addr = 0;
inline thread_local uint32_t g_longjmp_return_value = 0;

inline int FiberSetjmp(uint32_t ctx_addr) {
  g_setjmp_ctx_addr = ctx_addr;
  int ret = setjmp(g_fiber_jmp_buf);
  if (ret != 0) {
    REXLOG_INFO("FIBER: setjmp returning from longjmp (ret={})", ret);
  }
  return ret;
}

inline void FiberLongjmp(uint32_t return_value) {
  g_longjmp_return_value = return_value;
  REXLOG_INFO("FIBER: longjmp called with return_value={}", return_value);
  longjmp(g_fiber_jmp_buf, 1);
}

inline void FiberRestoreContext(PPCContext& ctx, uint8_t* base) {
  if (g_setjmp_ctx_addr == 0) return;
  uint32_t addr = g_setjmp_ctx_addr;
  uint32_t phys_offset = (addr >= 0xE0000000u) ? 0x1000u : 0u;
  uint8_t* ptr = base + addr + phys_offset;
  ctx.r1.u64 = __builtin_bswap64(*reinterpret_cast<uint64_t*>(ptr + 144));
  ctx.r31.u64 = __builtin_bswap64(*reinterpret_cast<uint64_t*>(ptr + 296));
  ctx.r3.u32 = g_longjmp_return_value;
  REXLOG_INFO("FIBER: restored r1=0x{:08X} r31=0x{:08X} r3={}",
              ctx.r1.u32, ctx.r31.u32, ctx.r3.s32);
}

inline float g_ultrawide_target_aspect = 0.0f;
inline uint32_t g_ultrawide_hook_call_count = 0;
inline uint32_t g_ultrawide_xscale_hook_call_count = 0;

constexpr double kNativeAspect = 1.7777778;

// Fix (upstream d6187367): use f64 consistently — reading the lower 32 bits of
// a double register as float produced denormal values, causing the hook guard
// (f29.f32 > 0.1f) to always fail. The aspect ratio lives in the full 64-bit
// slot, so we must read/write f64.
inline void UltrawideAspectHook(rex::ppc::Register& f29) {
  if (g_ultrawide_target_aspect > 0.0f &&
      f29.f64 > 0.1 && f29.f64 < 1000.0) {
    uint32_t count = ++g_ultrawide_hook_call_count;
    if (count == 1 || (count % 300) == 0) {
      REXLOG_INFO("ULTRAWIDE: y-scale hook #{}, keeping aspect at {:.4f} (was {:.4f})",
                  count, kNativeAspect, f29.f64);
    }
    f29.f64 = kNativeAspect;
  }
}

// Horizontal scale hook: scales the X column of the projection matrix so that
// geometry fills the wider viewport instead of being stretched.
inline void UltrawideXScaleHook(rex::ppc::Register& f12) {
  if (g_ultrawide_target_aspect > 0.0f) {
    double scale = kNativeAspect / static_cast<double>(g_ultrawide_target_aspect);
    uint32_t count = ++g_ultrawide_xscale_hook_call_count;
    if (count == 1 || (count % 300) == 0) {
      REXLOG_INFO("ULTRAWIDE: x-scale hook #{}, scaling m[0] by {:.4f} (f12={:.4f} -> {:.4f})",
                  count, scale, f12.f64, f12.f64 * scale);
    }
    f12.f64 = f12.f64 * scale;
  }
}
