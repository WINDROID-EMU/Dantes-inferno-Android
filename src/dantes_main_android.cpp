#include <SDL3/SDL_main.h>
#include <SDL3/SDL_hints.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
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

  MAIN_LOGI("Applied Graphics Config: res=%d, vsync=%d, effect=%s, async=%d, threads=%d, mode=%d",
            g_graphics_config.res_scale, g_graphics_config.vsync, g_graphics_config.present_effect.c_str(),
            g_graphics_config.async_shaders, g_graphics_config.pipeline_threads, g_graphics_config.present_mode);
}

int RunWindowedApp(int argc, char** argv) {
#if defined(__ANDROID__)
  rex::memory::AndroidInitialize();
  rex::thread::AndroidInitialize();
  rex::filesystem::AndroidInitialize();
#endif
  auto remaining = rex::cvar::Init(argc, argv);

  rex::cvar::SetFlagByName("async_shader_compilation", "true");
  rex::cvar::SetFlagByName("vulkan_pipeline_creation_threads", "4");
  rex::cvar::SetFlagByName("store_shaders", "true");

  rex::cvar::SetFlagByName("texture_cache_memory_limit_soft", "512");
  rex::cvar::SetFlagByName("texture_cache_memory_limit_hard", "768");
  rex::cvar::SetFlagByName("texture_cache_memory_limit_render_to_texture", "96");
  rex::cvar::SetFlagByName("texture_cache_memory_limit_soft_lifetime", "60");
  rex::cvar::SetFlagByName("gpu_allow_invalid_fetch_constants", "true");

  rex::cvar::SetFlagByName("vsync", "true");

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

  std::string current_log_level = REXCVAR_GET(log_level);
  if (disable_debug || current_log_level == "off") {
    rex::cvar::SetFlagByName("log_file", "");
    rex::cvar::SetFlagByName("log_level", "off");
    rex::cvar::SetFlagByName("log_verbose", "false");
    rex::cvar::SetFlagByName("log_noisy", "false");
    rex::cvar::SetFlagByName("log_high_frequency_kernel_calls", "false");
    rex::cvar::SetFlagByName("vulkan_validation_enabled", "false");
    rex::cvar::SetFlagByName("vulkan_log_debug_messages", "false");
    rex::cvar::SetFlagByName("gpu_debug_markers", "false");
    rex::cvar::SetFlagByName("kernel_debug_monitor", "false");
    rex::cvar::SetFlagByName("kernel_cert_monitor", "false");
  } else if (!ext.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(ext / "logs", ec);
    rex::cvar::SetFlagByName("log_file", (ext / "logs" / "dantes_inferno.log").string());
  }

  rex::InitLoggingEarly();

  // NOTE: InitializeDriver() and LogTextureCompressionSupport() are called
  // in DantesInfernoApp::OnPreSetup — do NOT call them here as well.

  int result = EXIT_FAILURE;
  {
#if defined(__ANDROID__)
    // Force SDL to respect the landscape orientation. Without this, SDL3
    // might override the AndroidManifest setting and switch to portrait (requestedOrientation=13).
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
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
    jboolean async_shaders, jint pipeline_threads, jint present_mode) {
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
  g_graphics_config.configured = true;

  MAIN_LOGI("JNI: nativeSetGraphicsConfig: res=%d, vsync=%d, effect=%s, async=%d, threads=%d, mode=%d",
            res_scale, vsync, g_graphics_config.present_effect.c_str(), async_shaders, pipeline_threads, present_mode);
}
#endif

