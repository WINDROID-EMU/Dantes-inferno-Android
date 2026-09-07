#include <SDL3/SDL_main.h>

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
#define MAIN_LOGI(...) do { if (!dantes::driver::GetDriverConfig().disable_debug) __android_log_print(ANDROID_LOG_INFO, "DantesMain", __VA_ARGS__); } while(0)
#define MAIN_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "DantesMain", __VA_ARGS__)
#else
#define MAIN_LOGI(...)
#define MAIN_LOGE(...)
#endif

namespace {

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

  bool disable_debug = dantes::driver::GetDriverConfig().disable_debug;

  if (!ext.empty()) {
    auto toml_path = ext / "dantes_inferno.toml";
    if (std::filesystem::exists(toml_path)) {
      rex::cvar::LoadConfig(toml_path);
    }
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

#if defined(__ANDROID__)
  dantes::driver::InitializeDriver();
  dantes::driver::LogTextureCompressionSupport();
#endif

  int result = EXIT_FAILURE;
  {
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
