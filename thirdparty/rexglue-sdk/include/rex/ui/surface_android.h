#pragma once

#include <rex/ui/surface.h>
#include <android/native_window.h>

namespace rex {
namespace ui {

class AndroidNativeWindowSurface final : public Surface {
 public:
  explicit AndroidNativeWindowSurface(ANativeWindow* window) : window_(window) {
    if (window_) {
      ANativeWindow_acquire(window_);
    }
  }

  ~AndroidNativeWindowSurface() override {
    if (window_) {
      ANativeWindow_release(window_);
    }
  }

  TypeIndex GetType() const override { return kTypeIndex_AndroidNativeWindow; }
  ANativeWindow* window() const { return window_; }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override {
    if (!window_) {
      return false;
    }
    int32_t w = ANativeWindow_getWidth(window_);
    int32_t h = ANativeWindow_getHeight(window_);
    if (w <= 0 || h <= 0) {
      return false;
    }
    width_out = static_cast<uint32_t>(w);
    height_out = static_cast<uint32_t>(h);
    return true;
  }

 private:
  ANativeWindow* window_ = nullptr;
};

}  // namespace ui
}  // namespace rex
