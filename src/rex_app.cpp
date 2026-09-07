/**
 * @file        ui/rex_app.cpp
 * @brief       ReXApp implementation - compiled as part of the consumer executable
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/rex_app.h>

#include <cstdlib>
#include <dlfcn.h>

#include <rex/assert.h>
#include <rex/cvar.h>
#include <rex/ui/flags.h>
#include <rex/kernel/crt/heap.h>
#include <rex/filesystem.h>
#include <rex/logging/sink.h>
#include <rex/logging.h>
#include <rex/ui/overlay/achievement_toast.h>
#include <rex/ui/overlay/achievements_overlay.h>
#include <rex/ui/overlay/console_overlay.h>
#include <rex/ui/overlay/debug_overlay.h>
#include <rex/ui/overlay/settings_overlay.h>
#include <rex/audio/audio_system.h>
#include <rex/audio/sdl/sdl_audio_system.h>
#include <rex/input/input_system.h>
#include <rex/kernel/init.h>
#include <rex/string/numeric.h>
#include <rex/system.h>
#include <rex/system/achievement_manager.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>
#include <rex/ui/graphics_provider.h>
#include <rex/ui/keybinds.h>
#include <rex/version.h>

#include <fmt/format.h>
#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <string_view>

REXCVAR_DEFINE_STRING(gpu_plugin, "", "GPU",
                      "GPU emulation plugin to load at startup (e.g. 'xenos'); empty disables "
                      "GPU emulation")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

#if defined(__ANDROID__)
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#include <android/log.h>
#include <pthread.h>
#include <elf.h>
#include <mutex>
#include <unordered_set>
#include <cstring>
#include <cerrno>

namespace {

static std::mutex g_thread_tracking_mutex;
static std::unordered_set<pthread_t> g_active_threads;
static std::unordered_set<pthread_t> g_joined_or_detached_threads;

static int (*g_real_pthread_create)(pthread_t*, const pthread_attr_t*, void*(*)(void*), void*) = nullptr;
static int (*g_real_pthread_join)(pthread_t, void**) = nullptr;
static int (*g_real_pthread_detach)(pthread_t) = nullptr;

static bool IsPointerMapped(const void* ptr) {
  uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  if (addr < 0x10000) return false;
  uintptr_t page_mask = sysconf(_SC_PAGESIZE) - 1;
  uintptr_t page = addr & ~page_mask;
  unsigned char vec = 0;
  return mincore(reinterpret_cast<void*>(page), 1, &vec) == 0;
}

#include <sys/resource.h>
#include <sched.h>

struct ThreadStartArg {
  void* (*real_start)(void*);
  void* real_arg;
  int detachstate;
};

static void ConfigurePerformanceThread() {
  // Set thread priority (nice -10 for high performance game threads)
  setpriority(PRIO_PROCESS, 0, -10);

  // Set CPU affinity to Big + Prime cores
  int num_cores = sysconf(_SC_NPROCESSORS_CONF);
  if (num_cores > 1) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    if (num_cores >= 8) {
      // Typically on Snapdragon 8-core: cores 4, 5, 6 are Gold/Big and 7 is Prime
      for (int i = 4; i < num_cores; ++i) {
        CPU_SET(i, &cpuset);
      }
    } else if (num_cores >= 4) {
      for (int i = num_cores / 2; i < num_cores; ++i) {
        CPU_SET(i, &cpuset);
      }
    } else {
      for (int i = 0; i < num_cores; ++i) {
        CPU_SET(i, &cpuset);
      }
    }
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
  }
}

static void* PerformanceThreadWrapper(void* arg) {
  auto* t_arg = reinterpret_cast<ThreadStartArg*>(arg);
  void* (*real_start)(void*) = t_arg->real_start;
  void* real_arg = t_arg->real_arg;
  delete t_arg;

  // Do NOT call ConfigurePerformanceThread() here for worker/streaming/audio threads.
  // Pinning ALL threads to the big/prime cores with nice -10 causes them to fight
  // for the same cores, starves background texture streaming (causing white textures),
  // leaves LITTLE cores completely idle, and triggers severe thermal throttling.
  // The Android Linux scheduler with EAS (Energy Aware Scheduling) manages
  // thread distribution across big.LITTLE cores automatically.
  // ConfigurePerformanceThread() is only called once for the main thread in InitBionicPthreadFix.

  return real_start(real_arg);
}

static int HookedPthreadCreate(pthread_t* thread, const pthread_attr_t* attr, void* (*start_routine)(void*), void* arg) {
  int detachstate = PTHREAD_CREATE_JOINABLE;
  if (attr) {
    pthread_attr_getdetachstate(attr, &detachstate);
  }
  if (!g_real_pthread_create) {
    g_real_pthread_create = (int(*)(pthread_t*, const pthread_attr_t*, void*(*)(void*), void*))dlsym(RTLD_DEFAULT, "pthread_create");
  }

  auto* t_arg = new ThreadStartArg{start_routine, arg, detachstate};
  int res = g_real_pthread_create(thread, attr, &PerformanceThreadWrapper, t_arg);
  if (res == 0 && thread && *thread) {
    std::lock_guard<std::mutex> lock(g_thread_tracking_mutex);
    if (detachstate == PTHREAD_CREATE_JOINABLE) {
      g_active_threads.insert(*thread);
    } else {
      g_joined_or_detached_threads.insert(*thread);
    }
  } else if (res != 0) {
    delete t_arg;
  }
  return res;
}

static int HookedPthreadDetach(pthread_t thread) {
  uintptr_t tval = static_cast<uintptr_t>(thread);
  if (tval < 0x10000) {
    __android_log_print(ANDROID_LOG_WARN, "BionicPthreadFix",
                        "Blocked pthread_detach on null/invalid ptr (%p)", reinterpret_cast<void*>(tval));
    return 0;
  }

  {
    std::lock_guard<std::mutex> lock(g_thread_tracking_mutex);
    if (g_joined_or_detached_threads.find(thread) != g_joined_or_detached_threads.end()) {
      __android_log_print(ANDROID_LOG_WARN, "BionicPthreadFix",
                          "Blocked duplicate pthread_detach on already joined/detached thread (%p)", reinterpret_cast<void*>(tval));
      return 0;
    }
  }

  if (!IsPointerMapped(reinterpret_cast<const void*>(tval))) {
    __android_log_print(ANDROID_LOG_WARN, "BionicPthreadFix",
                        "Blocked pthread_detach on unmapped memory (%p) to prevent Bionic abort", reinterpret_cast<void*>(tval));
    return 0;
  }

  if (!g_real_pthread_detach) {
    g_real_pthread_detach = (int(*)(pthread_t))dlsym(RTLD_DEFAULT, "pthread_detach");
  }

  int res = g_real_pthread_detach(thread);

  {
    std::lock_guard<std::mutex> lock(g_thread_tracking_mutex);
    g_active_threads.erase(thread);
    g_joined_or_detached_threads.insert(thread);
  }

  return res;
}

static int HookedPthreadJoin(pthread_t thread, void** retval) {
  uintptr_t tval = static_cast<uintptr_t>(thread);
  if (tval < 0x10000) {
    __android_log_print(ANDROID_LOG_WARN, "BionicPthreadFix",
                        "Blocked pthread_join on null/invalid ptr (%p)", reinterpret_cast<void*>(tval));
    if (retval) *retval = nullptr;
    return 0;
  }

  if (pthread_equal(thread, pthread_self())) {
    __android_log_print(ANDROID_LOG_WARN, "BionicPthreadFix",
                        "Blocked pthread_join on current thread (%p) to prevent deadlock", reinterpret_cast<void*>(tval));
    if (retval) *retval = nullptr;
    return EDEADLK;
  }

  {
    std::lock_guard<std::mutex> lock(g_thread_tracking_mutex);
    if (g_joined_or_detached_threads.find(thread) != g_joined_or_detached_threads.end()) {
      __android_log_print(ANDROID_LOG_WARN, "BionicPthreadFix",
                          "Blocked duplicate pthread_join on already joined/detached thread (%p)", reinterpret_cast<void*>(tval));
      if (retval) *retval = nullptr;
      return 0;
    }
  }

  // If the pointer memory is unmapped, it is a stale pointer that was freed/destroyed
  if (!IsPointerMapped(reinterpret_cast<const void*>(tval))) {
    __android_log_print(ANDROID_LOG_WARN, "BionicPthreadFix",
                        "Blocked pthread_join on unmapped memory (%p) to prevent Bionic abort", reinterpret_cast<void*>(tval));
    if (retval) *retval = nullptr;
    return 0;
  }

  if (!g_real_pthread_join) {
    g_real_pthread_join = (int(*)(pthread_t, void**))dlsym(RTLD_DEFAULT, "pthread_join");
  }

  int res = g_real_pthread_join(thread, retval);

  {
    std::lock_guard<std::mutex> lock(g_thread_tracking_mutex);
    g_active_threads.erase(thread);
    g_joined_or_detached_threads.insert(thread);
  }

  return res;
}

static bool PatchGotSlot(void* target_addr, void* new_func, void** old_func) {
  if (!target_addr) return false;
  uintptr_t page_size = sysconf(_SC_PAGESIZE);
  uintptr_t addr = (uintptr_t)target_addr;
  uintptr_t page_start = addr & ~(page_size - 1);
  uintptr_t page_end = (addr + sizeof(void*) - 1) & ~(page_size - 1);
  size_t len = page_end - page_start + page_size;

  if (mprotect((void*)page_start, len, PROT_READ | PROT_WRITE) != 0) {
    __android_log_print(ANDROID_LOG_ERROR, "BionicPthreadFix", "mprotect RW failed: %s", strerror(errno));
    return false;
  }

  if (old_func && !*old_func) {
    *old_func = *(void**)target_addr;
  }
  *(void**)target_addr = new_func;
  return true;
}

struct LibraryGotSlots {
  void** got_pthread_create = nullptr;
  void** got_pthread_join = nullptr;
  void** got_pthread_detach = nullptr;
};

static void ScanLibraryGot(const struct dl_phdr_info* info, LibraryGotSlots& out_slots) {
  ElfW(Addr) base = info->dlpi_addr;
  const ElfW(Phdr)* dynamic_phdr = nullptr;
  for (int i = 0; i < info->dlpi_phnum; ++i) {
    if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
      dynamic_phdr = &info->dlpi_phdr[i];
      break;
    }
  }
  if (!dynamic_phdr) return;

  const ElfW(Dyn)* dyn = reinterpret_cast<const ElfW(Dyn)*>(base + dynamic_phdr->p_vaddr);
  const ElfW(Rela)* jmprel = nullptr;
  size_t pltrelsz = 0;
  const ElfW(Rela)* rela = nullptr;
  size_t relasz = 0;
  const ElfW(Sym)* symtab = nullptr;
  const char* strtab = nullptr;

  for (; dyn->d_tag != DT_NULL; ++dyn) {
    switch (dyn->d_tag) {
      case DT_JMPREL:
        jmprel = reinterpret_cast<const ElfW(Rela)*>(
            dyn->d_un.d_ptr < base ? base + dyn->d_un.d_ptr : dyn->d_un.d_ptr);
        break;
      case DT_PLTRELSZ:
        pltrelsz = dyn->d_un.d_val;
        break;
      case DT_RELA:
        rela = reinterpret_cast<const ElfW(Rela)*>(
            dyn->d_un.d_ptr < base ? base + dyn->d_un.d_ptr : dyn->d_un.d_ptr);
        break;
      case DT_RELASZ:
        relasz = dyn->d_un.d_val;
        break;
      case DT_SYMTAB:
        symtab = reinterpret_cast<const ElfW(Sym)*>(
            dyn->d_un.d_ptr < base ? base + dyn->d_un.d_ptr : dyn->d_un.d_ptr);
        break;
      case DT_STRTAB:
        strtab = reinterpret_cast<const char*>(
            dyn->d_un.d_ptr < base ? base + dyn->d_un.d_ptr : dyn->d_un.d_ptr);
        break;
    }
  }

  auto scan_relas = [&](const ElfW(Rela)* r, size_t sz) {
    if (!r || !symtab || !strtab) return;
    size_t count = sz / sizeof(ElfW(Rela));
    for (size_t i = 0; i < count; ++i) {
      uint32_t sym_idx = ELF64_R_SYM(r[i].r_info);
      const char* sym_name = strtab + symtab[sym_idx].st_name;
      void** slot = reinterpret_cast<void**>(base + r[i].r_offset);
      if (strcmp(sym_name, "pthread_create") == 0 && !out_slots.got_pthread_create) {
        out_slots.got_pthread_create = slot;
      } else if (strcmp(sym_name, "pthread_join") == 0 && !out_slots.got_pthread_join) {
        out_slots.got_pthread_join = slot;
      } else if (strcmp(sym_name, "pthread_detach") == 0 && !out_slots.got_pthread_detach) {
        out_slots.got_pthread_detach = slot;
      }
    }
  };

  scan_relas(jmprel, pltrelsz);
  scan_relas(rela, relasz);
}

static int DlIteratePatchCallback(struct dl_phdr_info* info, size_t, void*) {
  if (!info->dlpi_name) return 0;

  bool is_rexruntime = strstr(info->dlpi_name, "librexruntimerd.so") != nullptr;
  bool is_downpour = strstr(info->dlpi_name, "libdantes_inferno.so") != nullptr;

  if (is_rexruntime || is_downpour) {
    LibraryGotSlots slots{};
    ScanLibraryGot(info, slots);

    if (is_rexruntime) {
      // Fallback to verified ELF relocations if dynamic scan did not locate all slots
      if (!slots.got_pthread_create) slots.got_pthread_create = reinterpret_cast<void**>(info->dlpi_addr + 0x85d728);
      if (!slots.got_pthread_join) slots.got_pthread_join = reinterpret_cast<void**>(info->dlpi_addr + 0x85de68);
      if (!slots.got_pthread_detach) slots.got_pthread_detach = reinterpret_cast<void**>(info->dlpi_addr + 0x866c88);

      PatchGotSlot(slots.got_pthread_create, (void*)&HookedPthreadCreate, (void**)&g_real_pthread_create);
      PatchGotSlot(slots.got_pthread_join, (void*)&HookedPthreadJoin, (void**)&g_real_pthread_join);
      PatchGotSlot(slots.got_pthread_detach, (void*)&HookedPthreadDetach, (void**)&g_real_pthread_detach);
      __android_log_print(ANDROID_LOG_INFO, "BionicPthreadFix",
                          "Patched librexruntimerd.so pthread GOT slots (create=%p, join=%p, detach=%p)",
                          slots.got_pthread_create, slots.got_pthread_join, slots.got_pthread_detach);
    } else if (is_downpour) {
      if (slots.got_pthread_create) {
        PatchGotSlot(slots.got_pthread_create, (void*)&HookedPthreadCreate, (void**)&g_real_pthread_create);
        __android_log_print(ANDROID_LOG_INFO, "BionicPthreadFix",
                            "Patched libdantes_inferno.so pthread_create GOT slot (%p)", slots.got_pthread_create);
      }
    }
  }
  return 0;
}

__attribute__((constructor(101)))
static void InitBionicPthreadFix() {
  if (!g_real_pthread_create) g_real_pthread_create = (int(*)(pthread_t*, const pthread_attr_t*, void*(*)(void*), void*))dlsym(RTLD_DEFAULT, "pthread_create");
  if (!g_real_pthread_join) g_real_pthread_join = (int(*)(pthread_t, void**))dlsym(RTLD_DEFAULT, "pthread_join");
  if (!g_real_pthread_detach) g_real_pthread_detach = (int(*)(pthread_t))dlsym(RTLD_DEFAULT, "pthread_detach");

  dl_iterate_phdr(DlIteratePatchCallback, nullptr);
  ConfigurePerformanceThread();
  __android_log_print(ANDROID_LOG_INFO, "BionicPthreadFix", "Bionic pthread hooks initialized successfully and main thread configured for Big.LITTLE performance cores!");
}

}  // namespace
#endif

namespace rex {

// --- ReXApp ---

ReXApp::~ReXApp() = default;

ReXApp::ReXApp(ui::WindowedAppContext& ctx, std::string_view name, PPCImageInfo ppc_info,
               std::string_view usage)
    : WindowedApp(ctx, name, usage), ppc_info_(ppc_info) {}

std::unique_ptr<ui::ImGuiDialog> ReXApp::CreateAchievementsOverlay() {
  if (!runtime_ || !runtime_->kernel_state() || !imgui_drawer_ || !immediate_drawer_) {
    return nullptr;
  }
  return std::make_unique<ui::AchievementsOverlayDialog>(
      imgui_drawer_.get(), immediate_drawer_.get(), runtime_.get(), &achievements());
}

std::unique_ptr<ui::AchievementNotificationDialog> ReXApp::CreateAchievementNotificationDialog() {
  if (!imgui_drawer_ || !immediate_drawer_ || !runtime_) {
    return nullptr;
  }
  return std::make_unique<ui::AchievementToastDialog>(imgui_drawer_.get(), immediate_drawer_.get(),
                                                      runtime_.get());
}

system::AchievementManager& ReXApp::achievements() const {
  assert_not_null(runtime_);
  assert_not_null(runtime_->kernel_state());
  return runtime_->kernel_state()->achievements();
}

bool ReXApp::OnInitialize() {
  if (!SetupEnvironment())
    return false;
  if (!SetupPresentation())
    return false;

  auto paths = OnFinalizePaths(resolved_defaults_, MakeResumeCallback());
  if (!paths) {
    // Async: consumer will invoke resume when ready. OnInitialize returns
    // true so the event loop keeps pumping (wizard dialogs render).
    return true;
  }

  if (!ConstructRuntime(*paths))
    return false;
  LaunchModule();
  return true;
}

bool ReXApp::SetupEnvironment() {
  auto exe_dir = rex::filesystem::GetExecutableFolder();

  std::filesystem::path game_dir;
  std::string game_data_cvar = REXCVAR_GET(game_data_root);
  if (!game_data_cvar.empty()) {
    game_dir = game_data_cvar;
  }

  // User data: cvar override, or platform user directory
  std::filesystem::path user_dir;
  std::string user_data_cvar = REXCVAR_GET(user_data_root);
  if (!user_data_cvar.empty()) {
    user_dir = user_data_cvar;
  } else {
    user_dir = rex::filesystem::GetUserFolder() / GetName();
  }

  // Update data: cvar override, or empty (opt-in)
  std::filesystem::path update_dir;
  std::string update_data_cvar = REXCVAR_GET(update_data_root);
  if (!update_data_cvar.empty()) {
    update_dir = update_data_cvar;
  }

  // Cache: cvar override, or user_dir/cache
  std::filesystem::path cache_dir;
  std::string cache_root_cvar = REXCVAR_GET(cache_root);
  if (!cache_root_cvar.empty()) {
    cache_dir = cache_root_cvar;
  } else {
    cache_dir = user_dir / "cache";
  }

  std::filesystem::path metadata_dir;
  std::string metadata_root_cvar = REXCVAR_GET(metadata_root);
  if (!metadata_root_cvar.empty()) {
    metadata_dir = metadata_root_cvar;
  }

  PathConfig path_config{game_dir,  user_dir,     update_dir,
                         cache_dir, metadata_dir, exe_dir / (std::string(GetName()) + ".toml")};
  OnConfigurePaths(path_config);
  game_data_root_ = path_config.game_data_root;
  user_data_root_ = path_config.user_data_root;
  update_data_root_ = path_config.update_data_root;
  cache_root_ = path_config.cache_root;
  metadata_root_ = path_config.metadata_root;
  config_path_ = path_config.config_path;
  resolved_defaults_ = std::move(path_config);

  // Load config FIRST so log cvars have final values
  if (std::filesystem::exists(config_path_))
    rex::cvar::LoadConfig(config_path_);

  // Late-phase logging
  std::string log_file_cvar = REXCVAR_GET(log_file);
  std::string log_level_str = REXCVAR_GET(log_level);
  if (REXCVAR_GET(log_verbose) && log_level_str == "info")
    log_level_str = "trace";

  auto category_levels = rex::ParseCategoryLevelsFromConfig(config_path_);
  auto log_config = rex::BuildLogConfig(log_file_cvar.empty() ? nullptr : log_file_cvar.c_str(),
                                        log_level_str, category_levels);
  if (log_level_str == "off") {
    log_config.default_level = spdlog::level::off;
    log_config.log_file = nullptr;
    log_config.log_dir.clear();
    log_config.app_name.clear();
  } else if (log_file_cvar.empty()) {
    log_config.app_name = std::string(GetName());
#if defined(__ANDROID__)
    auto logs_dir = user_data_root_.empty()
                        ? (std::filesystem::path("/storage/emulated/0/Android/data/com.dantesinferno.game/files") / "logs")
                        : (user_data_root_ / "logs");
    std::error_code ec;
    std::filesystem::create_directories(logs_dir, ec);
    log_config.log_dir = logs_dir.string();
#else
    log_config.log_dir = (exe_dir / "logs").string();
#endif
  }

  try {
    rex::InitLogging(log_config);
  } catch (const std::exception& e) {
    log_config.log_file = nullptr;
    log_config.app_name.clear();
    log_config.log_dir.clear();
    log_config.log_to_console = true;
    try {
      rex::InitLogging(log_config);
    } catch (...) {}
  }
  rex::RegisterLogLevelCallback();

  if (log_level_str == "off") {
    rex::SetAllLevels(spdlog::level::off);
  } else {
    log_sink_ = std::make_shared<rex::LogCaptureSink>();
    rex::AddSink(log_sink_);
  }

  OnPostInitLogging();

  if (std::filesystem::exists(config_path_))
    REXLOG_DEBUG("Loaded config: {}", config_path_.filename().string());

  REXLOG_DEBUG("{} starting", GetName());
  if (!game_data_root_.empty()) {
    REXLOG_DEBUG("  Game directory: {}", game_data_root_.string());
  }
  if (!user_data_root_.empty()) {
    REXLOG_DEBUG("  User data:      {}", user_data_root_.string());
  }
  if (!update_data_root_.empty()) {
    REXLOG_DEBUG("  Update data:    {}", update_data_root_.string());
  }
  REXLOG_DEBUG("  Cache root:     {}", cache_root_.string());
  if (!metadata_root_.empty()) {
    REXLOG_DEBUG("  Metadata root:  {}", metadata_root_.string());
  }

  return true;
}

bool ReXApp::ConstructRuntime(const PathConfig& paths) {
  if (paths.game_data_root.empty()) {
    auto msg = std::string("--game_data_root was not provided.");
    REXLOG_ERROR("{}", msg);
    rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
    return false;
  }
  if (!std::filesystem::is_directory(paths.game_data_root)) {
    auto msg = fmt::format("--game_data_root does not exist: {}", paths.game_data_root.string());
    REXLOG_ERROR("{}", msg);
    rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
    return false;
  }

  game_data_root_ = paths.game_data_root;
  user_data_root_ = paths.user_data_root;
  update_data_root_ = paths.update_data_root;
  cache_root_ = paths.cache_root;
  metadata_root_ = paths.metadata_root;

  runtime_ =
      std::make_unique<rex::Runtime>(paths.game_data_root, paths.user_data_root,
                                     paths.update_data_root, paths.cache_root, paths.metadata_root);
  runtime_->set_app_context(&app_context());

  // Window and ImGui drawer already exist from SetupPresentation; publish them
  // to the runtime before Setup so hooks and native rendering see them.
  if (window_) {
    runtime_->set_display_window(window_.get());
  }
  if (imgui_drawer_) {
    runtime_->set_imgui_drawer(imgui_drawer_.get());
  }

  auto status = runtime_->Setup(ppc_info_, std::move(config_));
  if (XFAILED(status)) {
    REXLOG_ERROR("Runtime setup failed: {:08X}", status);
    return false;
  }

  if (window_ && runtime_->input_system()) {
    static_cast<rex::input::InputSystem*>(runtime_->input_system())->AttachWindow(window_.get());
  }

  if (ppc_info_.register_modules) {
    ppc_info_.register_modules(runtime_->kernel_state());
  }

  if (imgui_drawer_) {
    auto* input_sys = static_cast<rex::input::InputSystem*>(runtime_->input_system());
    if (input_sys) {
      input_sys->SetActiveCallback([this]() {
        if (!debug_overlay_ && !console_overlay_ && !settings_overlay_ && !achievements_overlay_)
          return true;
        return !imgui_drawer_->GetIO().WantCaptureMouse;
      });
    }
  }

  std::string xex_image = "game:\\default.xex";
  OnLoadXexImage(xex_image);

  // Mirrors the game:\ / d:\ -> game_data_root mapping in Runtime::SetupVfs.
  {
    constexpr std::string_view kGameDevice = "game:\\";
    constexpr std::string_view kDDevice = "d:\\";
    std::string_view tail = xex_image;
    if (tail.starts_with(kGameDevice)) {
      tail.remove_prefix(kGameDevice.size());
    } else if (tail.starts_with(kDDevice)) {
      tail.remove_prefix(kDDevice.size());
    }
    std::string host_tail{tail};
    std::replace(host_tail.begin(), host_tail.end(), '\\', '/');
    auto xex_host = paths.game_data_root / host_tail;
    if (!std::filesystem::is_regular_file(xex_host)) {
      auto msg = fmt::format("Entrypoint XEX not found: {}", xex_host.string());
      REXLOG_ERROR("{}", msg);
      rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
      return false;
    }
  }

  status = runtime_->LoadXexImage(xex_image);
  if (XFAILED(status)) {
    auto msg = fmt::format("Failed to load XEX ({}): {:08X}", xex_image, status);
    REXLOG_ERROR("{}", msg);
    rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
    return false;
  }

  OnPostLoadXexImage();

  if (ppc_info_.rexcrt_heap) {
    if (!rex::kernel::crt::InitHeap(REXCVAR_GET(rexcrt_heap_size_mb), runtime_->memory())) {
      REXLOG_ERROR("Failed to initialize rexcrt heap");
      return false;
    }
  }

  OnPostSetup();

  return true;
}

bool ReXApp::SetupPresentation() {
  config_.gpu_plugin = REXCVAR_GET(gpu_plugin);
#if defined(__ANDROID__)
  if (config_.gpu_plugin.empty()) {
    config_.gpu_plugin = "xenos";
  }
#endif
  config_.audio_factory = REX_AUDIO_BACKEND(rex::audio::sdl::SDLAudioSystem);
  config_.input_factory = REX_INPUT_BACKEND(rex::input::CreateDefaultInputSystem);
  config_.kernel_init = rex::kernel::InitializeKernel;

  OnPreSetup(config_);

  if (!config_.graphics && !config_.gpu_plugin.empty()) {
#if defined(__ANDROID__)
    void* handle = dlopen("librexgpu-xenosrd.so", RTLD_NOW);
    if (!handle) {
      handle = dlopen("librexgpu-xenosd.so", RTLD_NOW);
    }
    if (!handle) {
      handle = dlopen("librexgpu-xenos.so", RTLD_NOW);
    }
    if (handle) {
      auto abi_fn = reinterpret_cast<rex::system::GpuAbiVersionFn>(
          dlsym(handle, rex::system::kGpuAbiVersionSymbol));
      auto create_fn = reinterpret_cast<rex::system::GpuCreateFn>(
          dlsym(handle, rex::system::kGpuCreateSymbol));
      if (abi_fn && create_fn) {
        rex::system::GpuCreateInfo info{};
        info.struct_size = sizeof(rex::system::GpuCreateInfo);
        info.backend = "vulkan";
        config_.graphics = std::unique_ptr<rex::system::IGraphicsSystem>(
            create_fn(rex::system::kGpuPluginAbiVersion, &info));
        REXLOG_INFO("Android: Successfully loaded GPU plugin (xenos/vulkan) via dlopen");
      }
    }
    if (!config_.graphics) {
      config_.graphics = rex::system::LoadGpuPlugin(config_.gpu_plugin);
    }
#else
    config_.graphics = rex::system::LoadGpuPlugin(config_.gpu_plugin);
#endif
    if (!config_.graphics) {
      // Fatal by design: no silent headless fallback.
      auto msg =
          fmt::format("Failed to load GPU plugin '{}'. See log for details.", config_.gpu_plugin);
      REXLOG_ERROR("{}", msg);
      rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
      return false;
    }
  }

  if (config_.graphics) {
    X_STATUS status = config_.graphics->SetupPresentation(&app_context());
    if (XFAILED(status)) {
      REXLOG_ERROR("Graphics presentation setup failed: {:08X}", status);
      return false;
    }
  }

  // Presentation window size for fullscreen mobile display (standard 1280x720)
  int32_t desired_w = 1280;
  int32_t desired_h = 720;
  if (REXCVAR_GET(window_width) >= 1280 && REXCVAR_GET(window_height) >= 720) {
    desired_w = REXCVAR_GET(window_width);
    desired_h = REXCVAR_GET(window_height);
  }
  REXLOG_INFO("Creating presentation window with resolution {}x{}", desired_w, desired_h);

  // Create window
  window_ = rex::ui::Window::Create(app_context(), GetName(), desired_w, desired_h);
  if (!window_) {
    REXLOG_ERROR("Failed to create window");
    return false;
  }

  // Set window title with SDK build stamp
  std::string title = std::string(GetName()) + " " + REXGLUE_BUILD_TITLE;
  window_->SetTitle(title);

  window_->AddListener(this);
  window_->AddInputListener(this, 0);

  if (REXCVAR_GET(fullscreen)) {
    window_->SetFullscreen(true);
  }
  rex::cvar::RegisterChangeCallback("fullscreen", [this](std::string_view, std::string_view value) {
    if (window_) {
      window_->SetFullscreen(rex::string::from_string<bool>(value, false));
    }
  });
  window_->Open();

  auto* graphics_system = config_.graphics.get();
  if (graphics_system && graphics_system->presenter()) {
    // SDK mode: the emulated-Xenos presenter drives the overlays.
    auto* presenter = graphics_system->presenter();
    auto* provider = graphics_system->provider();
    if (provider) {
      immediate_drawer_ = provider->CreateImmediateDrawer();
      if (immediate_drawer_) {
        immediate_drawer_->SetPresenter(presenter);
        SetupOverlays(presenter, immediate_drawer_.get());
      }
    }
    window_->SetPresenter(presenter);
  } else if (!graphics_system) {
    // Detached mode: the app brings its own renderer and drives its own paint
    // loop. ReXApp owns the returned drawer via immediate_drawer_.
    immediate_drawer_ = OnCreateImmediateDrawer();
    if (immediate_drawer_) {
      SetupOverlays(/*presenter=*/nullptr, immediate_drawer_.get());
      // No window_->SetPresenter, no drawer SetPresenter: the app owns the
      // surface and the present cadence.
    }
  }

  return true;
}

void ReXApp::SetupOverlays(rex::ui::Presenter* presenter, rex::ui::ImmediateDrawer* drawer) {
  imgui_drawer_ = std::make_unique<rex::ui::ImGuiDrawer>(
      window_.get(), 64, [this](ImFontAtlas* atlas) { OnConfigureFonts(atlas); },
      [this](ImGuiStyle& imgui_style, rex::ui::Style& ui_style) {
        OnConfigureStyle(imgui_style, ui_style);
      });
  // presenter is nullptr in detached mode; ImGuiDrawer tolerates that and the
  // gated eager font upload in SetImmediateDrawer is skipped (font uploads
  // lazily on the first Draw instead).
  imgui_drawer_->SetPresenterAndImmediateDrawer(presenter, drawer);
  rex::ui::RegisterBind("bind_debug_overlay", "F3", "Toggle debug overlay", [this] {
    if (debug_overlay_) {
      debug_overlay_.reset();
    } else {
      debug_overlay_ =
          std::make_unique<ui::DebugOverlayDialog>(imgui_drawer_.get(), frame_stats_provider_);
    }
  });
  rex::ui::RegisterBind("bind_console", "Backtick", "Toggle console overlay", [this] {
    if (console_overlay_) {
      console_overlay_.reset();
    } else {
      console_overlay_ = std::make_unique<ui::ConsoleDialog>(imgui_drawer_.get(), log_sink_);
    }
  });
  rex::ui::RegisterBind("bind_settings", "F4", "Toggle settings overlay", [this] {
    if (settings_overlay_) {
      settings_overlay_.reset();
    } else {
      settings_overlay_ = std::make_unique<ui::SettingsDialog>(imgui_drawer_.get(), config_path_);
    }
  });
  rex::ui::RegisterBind("bind_achievements", "F7", "Toggle achievements overlay", [this] {
    if (achievements_overlay_) {
      achievements_overlay_.reset();
    } else {
      achievements_overlay_ = CreateAchievementsOverlay();
    }
  });

  OnCreateDialogs(imgui_drawer_.get());
}

void ReXApp::LaunchModule() {
  app_context().CallInUIThreadDeferred([this]() {
    // Register the achievement notification callback now that the runtime and
    // KernelState are guaranteed to exist. Done here (not OnCreateDialogs)
    // because KernelState is null during SetupPresentation.
    if (!achievement_notification_) {
      achievement_notification_ =
          std::shared_ptr<ui::AchievementNotificationDialog>(CreateAchievementNotificationDialog());
    }
    if (achievement_notification_ && achievement_notification_listener_ == 0 && runtime_ &&
        runtime_->kernel_state()) {
      std::weak_ptr<ui::AchievementNotificationDialog> notification = achievement_notification_;
      achievement_notification_listener_ = achievements().RegisterNotificationCallback(
          [notification](const rex::system::AchievementEvent& event) {
            if (auto dialog = notification.lock()) {
              dialog->Push(event);
            }
          });
    }

    OnPreLaunchModule();

#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "DantesLaunch", "Calling runtime_->PrepareModuleLaunch()...");
#endif
    auto main_thread = runtime_->PrepareModuleLaunch();
    if (!main_thread) {
#if defined(__ANDROID__)
      __android_log_print(ANDROID_LOG_ERROR, "DantesLaunch", "PrepareModuleLaunch FAILED (null main_thread)");
#endif
      REXLOG_ERROR("Failed to launch module");
      app_context().QuitFromUIThread();
      return;
    }

#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "DantesLaunch", "PrepareModuleLaunch succeeded! Thread=%p. Resuming main_thread...", main_thread.get());
#endif

    auto* graphics_system = runtime_->graphics_system();
    if (graphics_system && !runtime_->cache_root().empty()) {
      uint32_t title_id = runtime_->kernel_state()->title_id();
      if (title_id != 0) {
        REXLOG_INFO("Initializing shader storage for title {:08X}...", title_id);
        graphics_system->InitializeShaderStorage(runtime_->cache_root(), title_id, true);
      }
    }

    OnPostLaunchModule(main_thread.get());
    main_thread->Resume();

    module_thread_ = std::thread([this, main_thread = std::move(main_thread)]() mutable {
#if defined(__ANDROID__)
      __android_log_print(ANDROID_LOG_INFO, "DantesLaunch", "module_thread_ waiting for guest thread...");
#endif
      main_thread->Wait(0, 0, 0, nullptr);
      OnGuestThreadExit(main_thread.get());
#if defined(__ANDROID__)
      __android_log_print(ANDROID_LOG_INFO, "DantesLaunch", "Guest thread exited! Requesting QuitFromUIThread...");
#endif
      REXLOG_INFO("Execution complete");
      if (!shutting_down_.load(std::memory_order_acquire)) {
        app_context().CallInUIThread([this]() { app_context().QuitFromUIThread(); });
      }
    });
  });
}

std::function<void(PathConfig)> ReXApp::MakeResumeCallback() {
  return [this](PathConfig paths) {
    if (shutting_down_.load(std::memory_order_acquire))
      return;
    if (!ConstructRuntime(std::move(paths))) {
      app_context().QuitFromUIThread();
      return;
    }
    LaunchModule();
  };
}

void ReXApp::OnKeyDown(ui::KeyEvent& e) {
  rex::ui::ProcessKeyEvent(e);
}

void ReXApp::OnClosing(ui::UIEvent& e) {
  (void)e;
  REXLOG_INFO("Window closing, shutting down...");
  shutting_down_.store(true, std::memory_order_release);
  if (runtime_ && runtime_->kernel_state()) {
    runtime_->kernel_state()->TerminateTitle();
  }
  // Hard-exit rather than run subsystem teardown, which can deadlock on a host
  // lock still held by a straggler TerminateTitle left running. Flush (not
  // ShutdownLogging, which frees loggers a straggler may still use); the OS
  // reclaims the rest.
  REXLOG_INFO("Title terminated; hard-exiting process.");
  rex::FlushLogging();
  std::_Exit(0);
}

bool ReXApp::OnCloseRequested(ui::UIEvent& e) {
  (void)e;
  return OnWindowCloseRequested();
}

void ReXApp::OnResize(ui::UISetupEvent& e) {
  (void)e;
  if (!window_) {
    return;
  }
  OnWindowPixelSizeChanged(window_->GetActualPhysicalWidth(), window_->GetActualPhysicalHeight());
  OnWindowResized(window_->GetActualLogicalWidth(), window_->GetActualLogicalHeight());
}

void ReXApp::OnDpiChanged(ui::UISetupEvent& e) {
  (void)e;
  if (!window_) {
    return;
  }
  OnDpiScaleChanged(float(window_->GetDpi()) / float(window_->GetMediumDpi()));
}

void ReXApp::OnGotFocus(ui::UISetupEvent& e) {
  (void)e;
  OnWindowFocusChanged(true);
}

void ReXApp::OnLostFocus(ui::UISetupEvent& e) {
  (void)e;
  OnWindowFocusChanged(false);
}

void ReXApp::OnMinimized(ui::UIEvent& e) {
  (void)e;
  OnWindowMinimized();
}

void ReXApp::OnRestored(ui::UIEvent& e) {
  (void)e;
  OnWindowRestored();
}

void ReXApp::OnDestroy() {
  // Notify subclass before cleanup
  OnShutdown();

  // Unregister overlay keybinds before destroying dialogs
  rex::ui::UnregisterBind("bind_debug_overlay");
  rex::ui::UnregisterBind("bind_console");
  rex::ui::UnregisterBind("bind_settings");
  rex::ui::UnregisterBind("bind_achievements");

  // ImGui cleanup (reverse of setup)
  if (achievement_notification_listener_ != 0) {
    if (runtime_ && runtime_->kernel_state()) {
      achievements().UnregisterCallback(achievement_notification_listener_);
    }
    achievement_notification_listener_ = 0;
  }
  achievement_notification_.reset();
  achievements_overlay_.reset();
  settings_overlay_.reset();
  console_overlay_.reset();
  debug_overlay_.reset();
  if (imgui_drawer_) {
    imgui_drawer_->SetPresenterAndImmediateDrawer(nullptr, nullptr);
#if defined(__ANDROID__)
    imgui_drawer_.release();
#else
    imgui_drawer_.reset();
#endif
  }
  // immediate_drawer_ was already unlinked from imgui_drawer_ above. Detach it
  // from its presenter so SDK mode runs OnLeavePresenter() before disposal; in
  // detached mode the drawer never had a presenter, so SetPresenter(nullptr) is
  // a no-op.
  if (immediate_drawer_) {
    immediate_drawer_->SetPresenter(nullptr);
#if defined(__ANDROID__)
    // On Android, VulkanImmediateDrawer resources are tied to the Vulkan driver / Scudo
    // heap allocations across shared libraries. Freeing here triggers Scudo "invalid chunk state"
    // abort during shutdown. Let OS process reclamation handle it.
    immediate_drawer_.release();
#else
    immediate_drawer_.reset();
#endif
  }
  if (runtime_) {
    runtime_->set_display_window(nullptr);
    runtime_->set_imgui_drawer(nullptr);
  }
  // Window/runtime cleanup
  if (window_) {
    window_->SetPresenter(nullptr);
  }
  if (module_thread_.joinable()) {
    module_thread_.join();
  }
  if (window_) {
    window_->RemoveInputListener(this);
    window_->RemoveListener(this);
  }
  runtime_.reset();
  window_.reset();
}

void ReXApp::SetGuestFrameStats(ui::DebugOverlayDialog::FrameStatsProvider provider) {
  frame_stats_provider_ = provider;
  if (debug_overlay_) {
    debug_overlay_->SetStatsProvider(provider);
  }
}

}  // namespace rex
