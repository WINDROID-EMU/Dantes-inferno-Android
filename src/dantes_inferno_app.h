#pragma once

#include <rex/rex_app.h>
#include <rex/cvar.h>
#include <rex/chrono/clock.h>
#include <rex/filesystem.h>
#include <rex/input/flags.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/logging/macros.h>

#include <array>
#include <chrono>
#include <cstring>
#include <cstdlib>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "dantes_inferno_hooks.h"

REXCVAR_DEFINE_DOUBLE(time_scalar, 1.0, "Gameplay",
                      "Guest time scaling factor (1.0 = normal, 50.0 = fast-forward)");

REXCVAR_DEFINE_BOOL(show_fps_overlay, false, "UI",
                    "Show FPS and frametime overlay (top-left corner)");

REXCVAR_DEFINE_STRING(glyph_family, "auto", "UI",
                      "Button glyph family: auto, xbox, or playstation");

REXCVAR_DEFINE_DOUBLE(ultrawide_target_aspect, 0.0, "Graphics",
                      "Target aspect ratio for ultrawide (0=disabled, 1.7778=16:9, "
                      "2.3889=21:9, 3.5556=32:9)");

class FpsOverlayDialog : public rex::ui::ImGuiDialog {
 public:
  explicit FpsOverlayDialog(rex::ui::ImGuiDrawer* drawer,
                            rex::graphics::CommandProcessor* command_processor)
      : rex::ui::ImGuiDialog(drawer),
        command_processor_(command_processor),
        last_time_(std::chrono::steady_clock::now()),
        last_guest_frame_count_(command_processor ? command_processor->counter() : 0) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    auto now = std::chrono::steady_clock::now();
    auto delta = std::chrono::duration<double, std::milli>(now - last_time_);
    last_time_ = now;

    uint64_t current_guest_frames =
        command_processor_ ? command_processor_->counter() : 0;
    uint64_t frames_delta = current_guest_frames - last_guest_frame_count_;
    last_guest_frame_count_ = current_guest_frames;

    double interval_ms = delta.count();
    double guest_fps = 0;
    double guest_ft_ms = 0;
    if (frames_delta > 0 && interval_ms > 0) {
      guest_fps = frames_delta * 1000.0 / interval_ms;
      guest_ft_ms = interval_ms / frames_delta;
    }

    frame_history_[history_idx_] = static_cast<float>(guest_ft_ms);
    history_idx_ = (history_idx_ + 1) % kHistorySize;

    if (smoothed_fps_ == 0.0) {
      smoothed_fps_ = guest_fps;
      smoothed_ft_ = guest_ft_ms;
    } else {
      smoothed_fps_ = smoothed_fps_ * 0.85 + guest_fps * 0.15;
      smoothed_ft_ = smoothed_ft_ * 0.85 + guest_ft_ms * 0.15;
    }

    g_guest_fps.store(static_cast<float>(smoothed_fps_), std::memory_order_relaxed);
    g_guest_frametime_ms.store(static_cast<float>(smoothed_ft_), std::memory_order_relaxed);

#if !defined(__ANDROID__)
    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.65f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);

    bool visible = true;
    if (ImGui::Begin("##fps_overlay", &visible,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoFocusOnAppearing)) {
      ImGui::SetWindowFontScale(2.0f);
      ImU32 fps_color = smoothed_fps_ >= 55.0f ? IM_COL32(80, 255, 80, 255) :
                       smoothed_fps_ >= 30.0f ? IM_COL32(255, 220, 60, 255) :
                                                IM_COL32(255, 80, 80, 255);
      ImGui::PushStyleColor(ImGuiCol_Text, fps_color);
      ImGui::Text("%.0f FPS", smoothed_fps_);
      ImGui::PopStyleColor();
      ImGui::SetWindowFontScale(1.0f);

      ImGui::SetWindowFontScale(1.3f);
      ImGui::Text("%.1f ms", smoothed_ft_);
      ImGui::SetWindowFontScale(1.0f);

      ImGui::Spacing();
      ImGui::PlotLines("##frametime", frame_history_.data(),
                       static_cast<int>(kHistorySize),
                       static_cast<int>(history_idx_), nullptr,
                       0.0f, 50.0f, ImVec2(220, 50));
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
#endif
  }

 private:
  static constexpr size_t kHistorySize = 120;
  std::array<float, kHistorySize> frame_history_{};
  size_t history_idx_ = 0;
  rex::graphics::CommandProcessor* command_processor_;
  std::chrono::steady_clock::time_point last_time_;
  uint64_t last_guest_frame_count_;
  double smoothed_fps_ = 0.0;
  double smoothed_ft_ = 0.0;
};

#include "touch_overlay.h"
#include "dantes_driver.h"
#include "dantes_iso_installer.h"

class DantesInfernoApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<DantesInfernoApp>(new DantesInfernoApp(ctx, "dantes_inferno",
        PPCImageConfig));
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (paths.game_data_root.empty()) {
      // Check environment override (e.g. passed from Android Activity/JNI)
      if (const char* env_root = std::getenv("DANTES_GAME_ROOT")) {
        std::filesystem::path p(env_root);
        std::error_code ec;
        if (std::filesystem::is_directory(p, ec)) {
          paths.game_data_root = p;
          REXLOG_INFO("PATHS: Found game_data_root via DANTES_GAME_ROOT: {}", p.string());
        } else if (!p.empty()) {
          paths.game_data_root = p;
        }
      }

      if (paths.game_data_root.empty()) {
        const std::filesystem::path candidates[] = {
            // Standard PC candidates
            rex::filesystem::GetExecutableFolder() / "game",
            std::filesystem::current_path() / "game",
            // Android storage candidates
            std::filesystem::path("/sdcard/DantesInferno/game"),
            std::filesystem::path("/sdcard/DantesInferno"),
            std::filesystem::path("/storage/emulated/0/DantesInferno/game"),
            std::filesystem::path("/storage/emulated/0/DantesInferno"),
            std::filesystem::path("/sdcard/Android/data/com.dantesinferno.game/files/game"),
            std::filesystem::path("/data/data/com.dantesinferno.game/files/game"),
        };
        for (const auto& candidate : candidates) {
          std::error_code ec;
          if (std::filesystem::is_directory(candidate, ec)) {
            paths.game_data_root = candidate;
            REXLOG_INFO("PATHS: Selected game_data_root: {}", candidate.string());
            break;
          }
        }
      }

      if (paths.game_data_root.empty()) {
        // Default target path if not yet created (for first-run ISO extraction)
#if defined(__ANDROID__)
        paths.game_data_root = "/storage/emulated/0/Android/data/com.dantesinferno.game/files/game";
#else
        paths.game_data_root = rex::filesystem::GetExecutableFolder() / "game";
#endif
        REXLOG_INFO("PATHS: Defaulted initial game_data_root to: {}", paths.game_data_root.string());
      }
    }

#if defined(__ANDROID__)
    // Ensure user_data_root and cache_root point to writable Android directories.
    // Without this, the SDK falls back to GetUserFolder() which resolves to /data
    // on Android (no $HOME set), creating "/data/.local" → Permission denied crash.
    if (paths.user_data_root.empty()) {
      std::filesystem::path ext_base("/storage/emulated/0/Android/data/com.dantesinferno.game/files");
      if (const char* env_root = std::getenv("DANTES_GAME_ROOT")) {
        std::filesystem::path p(env_root);
        // DANTES_GAME_ROOT points to .../files/game, go up to .../files
        if (p.filename() == "game") {
          ext_base = p.parent_path();
        } else {
          ext_base = p;
        }
      }
      paths.user_data_root = ext_base / "user_data";
      std::error_code ec;
      std::filesystem::create_directories(paths.user_data_root, ec);
      REXLOG_INFO("PATHS: Android user_data_root set to: {}", paths.user_data_root.string());
    }
    if (paths.cache_root.empty()) {
      paths.cache_root = paths.user_data_root.parent_path() / "cache";
      std::error_code ec;
      std::filesystem::create_directories(paths.cache_root, ec);
      REXLOG_INFO("PATHS: Android cache_root set to: {}", paths.cache_root.string());
    }
#endif
  }

  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults,
      std::function<void(rex::PathConfig)> resume) override {
    rex::PathConfig runtime_paths = defaults;
    const auto& game_root = runtime_paths.game_data_root;

#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "DantesApp",
                        "OnFinalizePaths: game_root='%s', is_installed=%d",
                        game_root.string().c_str(),
                        dantes::IsGameDataInstalled(game_root) ? 1 : 0);
#endif

    if (!dantes::IsGameDataInstalled(game_root)) {
      if (const char* iso = std::getenv("DANTES_INSTALL_ISO");
          iso != nullptr && *iso != '\0') {
        std::string error;
#if defined(__ANDROID__)
        __android_log_print(ANDROID_LOG_INFO, "DantesApp",
                            "Attempting automatic install from DANTES_INSTALL_ISO='%s' to '%s'",
                            iso, game_root.string().c_str());
#endif
        REXLOG_INFO("Installing game data from DANTES_INSTALL_ISO={}", iso);
        if (!dantes::InstallGameDataFromIso(iso, game_root, nullptr, nullptr, error)) {
#if defined(__ANDROID__)
          __android_log_print(ANDROID_LOG_ERROR, "DantesApp",
                              "Automated game data installation failed: %s", error.c_str());
#endif
          REXLOG_ERROR("Automated game data installation failed: {}", error);
        } else {
#if defined(__ANDROID__)
          __android_log_print(ANDROID_LOG_INFO, "DantesApp",
                              "Automated game data installation succeeded!");
#endif
        }
      }
    }

    if (!dantes::IsGameDataInstalled(game_root)) {
#if defined(__ANDROID__)
      __android_log_print(ANDROID_LOG_WARN, "DantesApp",
                          "Game data still not installed at '%s'; launching disc image wizard",
                          game_root.string().c_str());
#endif
      REXLOG_INFO(
          "Dante's Inferno game data not found at {}; launching the "
          "disc image installer.",
          game_root.string());
      dantes::ShowIsoInstallWizard(imgui_drawer(), std::move(runtime_paths),
                                   std::move(resume));
      return std::nullopt;
    }

#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "DantesApp",
                        "OnFinalizePaths returning valid runtime_paths! Game is ready to launch.");
#endif
    return runtime_paths;
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    config.gpu_plugin = "xenos";

    REXCVAR_SET(input_backend, std::string("sdl"));

#if defined(__ANDROID__)
    // Mobile-specific defaults & AdrenoTools Turnip driver initialization
    // Disable SDK's ImGui touch controls because we use the native Android virtual controller
    rex::cvar::SetFlagByName("show_touch_controls", "false");
    rex::cvar::SetFlagByName("show_fps_overlay", "true");
    rex::cvar::SetFlagByName("mnk_mode", "false");
    dantes::driver::InitializeDriver();
    dantes::driver::LogTextureCompressionSupport();
#else
    rex::cvar::SetFlagByName("mnk_mode", "true");
    rex::cvar::SetFlagByName("mnk_mouse", "true");
    rex::cvar::SetFlagByName("mnk_sensitivity", "1.5");
#endif

    rex::cvar::SetFlagByName("keybind_a", "Space");
    rex::cvar::SetFlagByName("keybind_b", "F");
    rex::cvar::SetFlagByName("keybind_x", "MouseLeft");
    rex::cvar::SetFlagByName("keybind_y", "E");
    rex::cvar::SetFlagByName("keybind_left_shoulder", "Q");
    rex::cvar::SetFlagByName("keybind_right_shoulder", "MouseRight");
    rex::cvar::SetFlagByName("keybind_left_trigger", "Shift");
    rex::cvar::SetFlagByName("keybind_right_trigger", "Ctrl");
    rex::cvar::SetFlagByName("keybind_lstick_up", "W");
    rex::cvar::SetFlagByName("keybind_lstick_down", "S");
    rex::cvar::SetFlagByName("keybind_lstick_left", "A");
    rex::cvar::SetFlagByName("keybind_lstick_right", "D");
    rex::cvar::SetFlagByName("keybind_lstick_press", "X");
    rex::cvar::SetFlagByName("keybind_rstick_up", "Up");
    rex::cvar::SetFlagByName("keybind_rstick_down", "Down");
    rex::cvar::SetFlagByName("keybind_rstick_left", "Left");
    rex::cvar::SetFlagByName("keybind_rstick_right", "Right");
    rex::cvar::SetFlagByName("keybind_rstick_press", "R");
    rex::cvar::SetFlagByName("keybind_dpad_up", "Shift+Up");
    rex::cvar::SetFlagByName("keybind_dpad_down", "Shift+Down");
    rex::cvar::SetFlagByName("keybind_dpad_left", "Shift+Left");
    rex::cvar::SetFlagByName("keybind_dpad_right", "Shift+Right");
    rex::cvar::SetFlagByName("keybind_back", "Tab");
    rex::cvar::SetFlagByName("keybind_start", "Escape");

    double target_aspect = REXCVAR_GET(ultrawide_target_aspect);
    if (target_aspect > 0.0) {
      g_ultrawide_target_aspect = static_cast<float>(target_aspect);
      if (target_aspect >= 1.7778) {
        rex::cvar::SetFlagByName("present_letterbox", "false");
        REXLOG_INFO("ULTRAWIDE: target_aspect={:.4f}, present_letterbox disabled",
                    target_aspect);
      } else {
        rex::cvar::SetFlagByName("present_letterbox", "true");
        REXLOG_INFO("ULTRAWIDE: target_aspect={:.4f}, present_letterbox enabled",
                    target_aspect);
      }
    } else {
      REXLOG_INFO("ULTRAWIDE: disabled (target_aspect={:.4f})", target_aspect);
    }
  }

  void OnPreLaunchModule() override {
    uint8_t* membase = runtime()->memory()->virtual_membase();

    auto* ptr = reinterpret_cast<uint32_t*>(membase + 0x82B101E4);
    *ptr = 0u;
  }

  void OnPostSetup() override {
    rex::chrono::Clock::set_guest_time_scalar(REXCVAR_GET(time_scalar));

    rex::cvar::RegisterChangeCallback("time_scalar",
        [](std::string_view, std::string_view new_value) {
          double scalar = std::stod(std::string(new_value));
          if (scalar < 0.0) scalar = 0.0;
          rex::chrono::Clock::set_guest_time_scalar(scalar);
        });

    rex::cvar::RegisterChangeCallback("ultrawide_target_aspect",
        [](std::string_view, std::string_view new_value) {
          double aspect = std::stod(std::string(new_value));
          g_ultrawide_target_aspect = static_cast<float>(aspect);
          if (aspect >= 1.7778) {
            rex::cvar::SetFlagByName("present_letterbox", "false");
          } else {
            rex::cvar::SetFlagByName("present_letterbox", "true");
          }
        });

    rex::cvar::RegisterChangeCallback("show_touch_controls",
        [this](std::string_view, std::string_view new_value) {
          bool show = (new_value == "true" || new_value == "1");
          if (show && !touch_overlay_ && imgui_drawer()) {
            touch_overlay_ = std::make_unique<TouchOverlayDialog>(imgui_drawer());
          } else if (!show && touch_overlay_) {
            touch_overlay_.reset();
          }
        });

    rex::ui::RegisterBind("bind_exit_game", "Alt+F4",
                          "Exit game to desktop", [this] {
      app_context().RequestDeferredQuit();
    });

    if (REXCVAR_GET(show_fps_overlay) && imgui_drawer()) {
      auto* gfx_sys = runtime() ? runtime()->graphics_system() : nullptr;
      auto* command_processor = gfx_sys
          ? static_cast<rex::graphics::GraphicsSystem*>(gfx_sys)->command_processor()
          : nullptr;
      fps_overlay_ = std::make_unique<FpsOverlayDialog>(imgui_drawer(), command_processor);
    }

    if (REXCVAR_GET(show_touch_controls) && imgui_drawer()) {
      touch_overlay_ = std::make_unique<TouchOverlayDialog>(imgui_drawer());
    }

    rex::ui::RegisterBind("bind_fps_overlay", "F1",
                          "Toggle FPS overlay", [this] {
      if (fps_overlay_) {
        fps_overlay_.reset();
      } else if (imgui_drawer()) {
        auto* gfx_sys = runtime() ? runtime()->graphics_system() : nullptr;
        auto* command_processor = gfx_sys
            ? static_cast<rex::graphics::GraphicsSystem*>(gfx_sys)->command_processor()
            : nullptr;
        fps_overlay_ = std::make_unique<FpsOverlayDialog>(imgui_drawer(), command_processor);
      }
    });

    rex::ui::RegisterBind("bind_fast_forward", "F2",
                          "Toggle 50x fast-forward", [this] {
      double current = REXCVAR_GET(time_scalar);
      bool fast = current > 1.0;
      double target = fast ? 1.0 : 50.0;
      rex::cvar::SetFlagByName("time_scalar", std::to_string(target));
      rex::chrono::Clock::set_guest_time_scalar(target);
      rex::cvar::SetFlagByName("vsync", fast ? "true" : "false");
    });
  }

  void OnShutdown() override {
    rex::ui::UnregisterBind("bind_fast_forward");
    rex::ui::UnregisterBind("bind_fps_overlay");
    rex::ui::UnregisterBind("bind_exit_game");
    rex::cvar::UnregisterChangeCallbacks("time_scalar");
    rex::cvar::UnregisterChangeCallbacks("ultrawide_target_aspect");
    rex::cvar::UnregisterChangeCallbacks("show_touch_controls");
    touch_overlay_.reset();
    fps_overlay_.reset();
    rex::chrono::Clock::set_guest_time_scalar(1.0);
    rex::cvar::SetFlagByName("vsync", "true");
#if defined(__ANDROID__)
    dantes::driver::ShutdownDriver();
#endif
  }

 private:
  std::unique_ptr<FpsOverlayDialog> fps_overlay_;
  std::unique_ptr<TouchOverlayDialog> touch_overlay_;
};
