#include <SDL3/SDL_main.h>
#include <SDL3/SDL_hints.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/platform.h>
#include <rex/thread.h>
#include <rex/ui/windowed_app.h>
#include <rex/ui/windowed_app_context_sdl.h>

#include "generated/default/dantes_inferno_init.h"
#include "dantes_inferno_app.h"
#include "dantes_driver.h"

#if defined(__ANDROID__)
#include <android/log.h>
#define MAIN_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "DantesMain", __VA_ARGS__)
#define MAIN_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "DantesMain", __VA_ARGS__)
#else
#define MAIN_LOGI(...)
#define MAIN_LOGE(...)
#endif

namespace {

struct GraphicsConfig {
  int res_scale = 1;
  bool vsync = true;
  std::string present_effect = "fxaa";
  bool async_shaders = true;
  int pipeline_threads = -1;
  int present_mode = 0;
  int anisotropic = 0; // 0 = off by default on mobile
  bool configured = false;
};
static GraphicsConfig g_graphics_config;

void ApplyGraphicsConfig() {
  if (!g_graphics_config.configured) return;
  rex::cvar::SetFlagByName("resolution_scale", std::to_string(g_graphics_config.res_scale));
  rex::cvar::SetFlagByName("draw_resolution_scale_x", std::to_string(g_graphics_config.res_scale));
  rex::cvar::SetFlagByName("draw_resolution_scale_y", std::to_string(g_graphics_config.res_scale));
  rex::cvar::SetFlagByName("vsync", g_graphics_config.vsync ? "true" : "false");
  rex::cvar::SetFlagByName("swap_post_effect", g_graphics_config.present_effect);
  rex::cvar::SetFlagByName("anisotropic_override", std::to_string(g_graphics_config.anisotropic));
  rex::cvar::SetFlagByName("async_shader_compilation", g_graphics_config.async_shaders ? "true" : "false");
  rex::cvar::SetFlagByName("vulkan_pipeline_creation_threads", std::to_string(g_graphics_config.pipeline_threads));

  if (g_graphics_config.present_mode == 1) {
    rex::cvar::SetFlagByName("vulkan_allow_present_mode_mailbox", "true");
    rex::cvar::SetFlagByName("vulkan_allow_present_mode_immediate", "false");
  } else if (g_graphics_config.present_mode == 2) {
    rex::cvar::SetFlagByName("vulkan_allow_present_mode_immediate", "true");
    rex::cvar::SetFlagByName("vulkan_allow_present_mode_mailbox", "false");
  } else {
    rex::cvar::SetFlagByName("vulkan_allow_present_mode_mailbox", "false");
    rex::cvar::SetFlagByName("vulkan_allow_present_mode_immediate", "false");
  }

  MAIN_LOGI("Applied Graphics Config: res=%d, vsync=%d, effect=%s, aniso=%d, async=%d, threads=%d, mode=%d",
            g_graphics_config.res_scale, g_graphics_config.vsync, g_graphics_config.present_effect.c_str(),
            g_graphics_config.anisotropic, g_graphics_config.async_shaders, g_graphics_config.pipeline_threads, g_graphics_config.present_mode);
}

int RunWindowedApp(int argc, char** argv) {
#if defined(__ANDROID__)
  rex::memory::AndroidInitialize();
  rex::thread::AndroidInitialize();
  rex::filesystem::AndroidInitialize();

  // CRITICAL: Initialize AdrenoTools / Turnip driver BEFORE any Vulkan initialization!
  // ReXApp::SetupPresentation() creates the VkInstance and VkDevice during OnInitialize().
  // If InitializeDriver() is called late in OnPreSetup(), the VkInstance is already created
  // by the Qualcomm proprietary driver, creating a fatal handle mismatch and black screen with Turnip.
  dantes::driver::InitializeDriver();
  dantes::driver::LogTextureCompressionSupport();
#endif
  auto remaining = rex::cvar::Init(argc, argv);

  rex::cvar::SetFlagByName("async_shader_compilation", "true");
  rex::cvar::SetFlagByName("vulkan_async_skip_incomplete_frames", "true");
  rex::cvar::SetFlagByName("render_target_path_vulkan", "fbo");
  rex::cvar::SetFlagByName("vulkan_pipeline_creation_threads", "6");
  rex::cvar::SetFlagByName("store_shaders", "true");

  rex::cvar::SetFlagByName("texture_cache_memory_limit_soft", "512");
  rex::cvar::SetFlagByName("texture_cache_memory_limit_hard", "768");
  rex::cvar::SetFlagByName("texture_cache_memory_limit_render_to_texture", "96");
  rex::cvar::SetFlagByName("texture_cache_memory_limit_soft_lifetime", "60");
  rex::cvar::SetFlagByName("vsync", "true");
  rex::cvar::SetFlagByName("audio_maxqframes", "256");

  // Critical for Android ARM64 big.LITTLE / DynamIQ CPU topologies (e.g. Snapdragon):
  // Prevent guest threads from being pinned to host cores 0..3 (LITTLE low-power A510/A55 cores).
  // This allows the Linux kernel scheduler to run CPU-intensive recompilation and GPU threads
  // across all Big (Cortex-A710/A78) and Prime (Cortex-X2/X1) performance cores.
  rex::cvar::SetFlagByName("ignore_thread_affinities", "true");
  rex::cvar::SetFlagByName("ignore_thread_priorities", "true");

  // Fix for Qualcomm Adreno proprietary Vulkan drivers (Adreno 730/740/830):
  // Sparse buffer residency causes an uncatchable abort in Qualcomm proprietary drivers.
  // Disabling sparse shared memory forces the robust 512 MB fully-bound buffer fallback.
  rex::cvar::SetFlagByName("vulkan_sparse_shared_memory", "false");
  rex::cvar::SetFlagByName("vulkan_push_constants_descriptors", "false");
  rex::cvar::SetFlagByName("vulkan_deferred_resolve_clears", "false");

  const char* env_root = std::getenv("DANTES_GAME_ROOT");
  std::filesystem::path ext = env_root ? std::filesystem::path(env_root) : std::filesystem::path("/storage/emulated/0/Android/data/com.dantesinferno.game/files");

#if defined(__ANDROID__)
  // Set user_data_root and cache_root to writable Android app directories.
  // Without this, the SDK's SetupEnvironment() calls GetUserFolder() which
  // resolves to /data on Android (no $HOME), then tries to create
  // "/data/.local" → Permission denied → fatal crash in the GPU thread.
  //
  // DANTES_GAME_ROOT typically points to .../files/game — we need the parent
  // (.../files) so user_data/ and cache/ are siblings of game/, not inside it.
  {
    std::filesystem::path ext_base = ext;
    if (ext_base.filename() == "game") {
      ext_base = ext_base.parent_path();
    }
    auto user_data = ext_base / "user_data";
    auto cache_data = ext_base / "cache";
    std::error_code ec;
    std::filesystem::create_directories(user_data, ec);
    std::filesystem::create_directories(cache_data, ec);
    rex::cvar::SetFlagByName("user_data_root", user_data.string());
    rex::cvar::SetFlagByName("cache_root", cache_data.string());
    MAIN_LOGI("Set user_data_root=%s", user_data.string().c_str());
    MAIN_LOGI("Set cache_root=%s", cache_data.string().c_str());
  }
#endif

  bool disable_debug = dantes::driver::GetDriverConfig().disable_debug;

  if (!ext.empty()) {
    auto toml_path = ext / "dantes_inferno.toml";
    if (std::filesystem::exists(toml_path)) {
      rex::cvar::LoadConfig(toml_path);
    }
    if (ext.filename() == "game") {
      auto parent_toml = ext.parent_path() / "dantes_inferno.toml";
      if (std::filesystem::exists(parent_toml)) {
        rex::cvar::LoadConfig(parent_toml);
      }
    }
    ApplyGraphicsConfig();
  } else {
    ApplyGraphicsConfig();
  }

  if (!ext.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(ext / "logs", ec);
    std::string log_path = (ext / "logs" / "dantes_inferno.log").string();
    rex::cvar::SetFlagByName("log_file", log_path);
    rex::cvar::SetFlagByName("log_level", "info");

    // Forward ReXGlue engine logs to Android logcat under tag 'ReXEngine'
    std::thread([log_path]() {
      for (int i = 0; i < 50 && !std::filesystem::exists(log_path); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      std::ifstream file(log_path);
      if (!file.is_open()) return;
      std::string line;
      while (true) {
        while (std::getline(file, line)) {
          if (!line.empty()) {
            __android_log_print(ANDROID_LOG_INFO, "ReXEngine", "%s", line.c_str());
          }
        }
        file.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
    }).detach();
  }

  rex::InitLoggingEarly();

  int result = EXIT_FAILURE;
  {
#if defined(__ANDROID__)
    // Force SDL to respect the landscape orientation. Without this, SDL3
    // might override the AndroidManifest setting and switch to portrait (requestedOrientation=13).
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");

    // Prevent audio buffer underruns / stuttering during video playback and heavy CPU load.
    // Configure SDL3 to use Android's native AAudio backend with Game role and safety buffer.
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "AAudio", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DEVICE_STREAM_ROLE, "Game", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_ANDROID_LOW_LATENCY_AUDIO, "0", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "4096", SDL_HINT_OVERRIDE);
#endif
    MAIN_LOGI("Initializing SDLWindowedAppContext...");
    rex::ui::SDLWindowedAppContext app_context;
    if (!app_context.Initialize()) {
      MAIN_LOGE("app_context.Initialize() failed!");
      return EXIT_FAILURE;
    }
    MAIN_LOGI("SDLWindowedAppContext initialized successfully.");

    std::unique_ptr<rex::ui::WindowedApp> app = DantesInfernoApp::Create(app_context);

    const auto& option_names = app->GetPositionalOptions();
    std::map<std::string, std::string> parsed;
    size_t count = std::min(remaining.size(), option_names.size());
    for (size_t i = 0; i < count; ++i) {
      parsed[option_names[i]] = remaining[i];
    }
    app->SetParsedArguments(std::move(parsed));

    MAIN_LOGI("Calling app->OnInitialize()...");
    bool init_ok = app->OnInitialize();
    MAIN_LOGI("app->OnInitialize() returned: %s", init_ok ? "SUCCESS" : "FAILED");

    result = init_ok ? app_context.RunMainMessageLoop() : EXIT_FAILURE;
    MAIN_LOGI("RunMainMessageLoop() exited with code: %d. Beginning teardown...", result);

    app->InvokeOnDestroy();
#if defined(__ANDROID__)
    MAIN_LOGI("Shutting down driver and Android subsystems...");
    dantes::driver::ShutdownDriver();
    rex::filesystem::AndroidShutdown();
    rex::thread::AndroidShutdown();
    rex::memory::AndroidShutdown();
#endif
  }

  return result;
}

}  // namespace

extern "C" SDLMAIN_DECLSPEC int SDLCALL SDL_main(int argc, char* argv[]) {
  return RunWindowedApp(argc, argv);
}

#if defined(__ANDROID__)
#include <jni.h>

extern "C" JNIEXPORT void JNICALL
Java_com_dantesinferno_game_MainActivity_setGameRootEnv(JNIEnv* env, jobject /* thiz */, jstring path) {
  if (!path) return;
  const char* native_str = env->GetStringUTFChars(path, nullptr);
  if (native_str) {
    setenv("DANTES_GAME_ROOT", native_str, 1);
    MAIN_LOGI("JNI: setenv DANTES_GAME_ROOT=%s", native_str);
    env->ReleaseStringUTFChars(path, native_str);
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_dantesinferno_game_MainActivity_nativeOnIsoPicked(JNIEnv* env, jobject /* thiz */, jstring path) {
  if (!path) {
    dantes::android::SetPendingIsoPath("");
    return;
  }
  const char* native_str = env->GetStringUTFChars(path, nullptr);
  if (native_str) {
    setenv("DANTES_INSTALL_ISO", native_str, 1);
    dantes::android::SetPendingIsoPath(native_str);
    MAIN_LOGI("JNI: setenv DANTES_INSTALL_ISO=%s", native_str);
    env->ReleaseStringUTFChars(path, native_str);
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_dantesinferno_game_MainActivity_nativeSetGraphicsConfig(
    JNIEnv* env, jobject /* thiz */,
    jint res_scale, jboolean vsync, jstring present_effect,
    jboolean async_shaders, jint pipeline_threads, jint present_mode, jint anisotropic) {
  g_graphics_config.res_scale = res_scale;
  g_graphics_config.vsync = vsync;
  if (present_effect) {
    const char* effect_str = env->GetStringUTFChars(present_effect, nullptr);
    if (effect_str) {
      g_graphics_config.present_effect = effect_str;
      env->ReleaseStringUTFChars(present_effect, effect_str);
    }
  }
  g_graphics_config.async_shaders = async_shaders;
  g_graphics_config.pipeline_threads = pipeline_threads;
  g_graphics_config.present_mode = present_mode;
  g_graphics_config.anisotropic = anisotropic;
  g_graphics_config.configured = true;

  MAIN_LOGI("JNI: nativeSetGraphicsConfig: res=%d, vsync=%d, effect=%s, aniso=%d, async=%d, threads=%d, mode=%d",
            res_scale, vsync, g_graphics_config.present_effect.c_str(), anisotropic, async_shaders, pipeline_threads, present_mode);
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_dantesinferno_game_MainActivity_nativeGetEngineFps(JNIEnv* /* env */, jclass /* clazz */) {
  if (g_guest_frame_count.load(std::memory_order_relaxed) == 0) {
    return 0.0f;
  }
  auto now = std::chrono::steady_clock::now();
  double elapsed_ms = std::chrono::duration<double, std::milli>(now - g_last_swap_time).count();
  if (elapsed_ms > 250.0) {
    // Engine is stalled/frozen (e.g. compiling shaders or loading); degrade FPS accurately
    return static_cast<float>(1000.0 / elapsed_ms);
  }
  return g_guest_fps.load(std::memory_order_relaxed);
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_dantesinferno_game_MainActivity_nativeGetEngineFrametime(JNIEnv* /* env */, jclass /* clazz */) {
  if (g_guest_frame_count.load(std::memory_order_relaxed) == 0) {
    return 0.0f;
  }
  auto now = std::chrono::steady_clock::now();
  double elapsed_ms = std::chrono::duration<double, std::milli>(now - g_last_swap_time).count();
  if (elapsed_ms > 250.0) {
    return static_cast<float>(elapsed_ms);
  }
  return g_guest_frametime_ms.load(std::memory_order_relaxed);
}
#endif

