#include "dantes_driver.h"

#if defined(__ANDROID__)

#include <adrenotools/driver.h>
#include <SDL3/SDL.h>
#include <jni.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#include <android/log.h>
#include <elf.h>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <mutex>

#define LOG_TAG "DantesDriver"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace dantes::driver {

namespace {

static DriverConfig g_driver_config;
static std::mutex g_driver_mutex;
static void* g_adrenotools_vulkan_handle = nullptr;
static void* (*g_real_dlopen)(const char* filename, int flags) = nullptr;
static int (*g_real_dlclose)(void* handle) = nullptr;

static void* HookedDlopen(const char* filename, int flags) {
  if (filename && g_adrenotools_vulkan_handle) {
    if (strstr(filename, "libvulkan.so") != nullptr || strcmp(filename, "vulkan") == 0) {
      LOGI("HookedDlopen: Intercepted '%s' -> returning AdrenoTools Turnip handle (%p)",
           filename, g_adrenotools_vulkan_handle);
      return g_adrenotools_vulkan_handle;
    }
  }
  if (!g_real_dlopen) {
    g_real_dlopen = reinterpret_cast<void*(*)(const char*, int)>(dlsym(RTLD_DEFAULT, "dlopen"));
  }
  return g_real_dlopen ? g_real_dlopen(filename, flags) : nullptr;
}

static int HookedDlclose(void* handle) {
  if (handle && g_adrenotools_vulkan_handle && handle == g_adrenotools_vulkan_handle) {
    LOGI("HookedDlclose: Preserving AdrenoTools Turnip handle (%p) against dlclose call", handle);
    return 0; // Shield the custom Vulkan driver from premature dlclose unmapping
  }
  if (!g_real_dlclose) {
    g_real_dlclose = reinterpret_cast<int(*)(void*)>(dlsym(RTLD_DEFAULT, "dlclose"));
  }
  return g_real_dlclose ? g_real_dlclose(handle) : 0;
}

static bool PatchGotSlot(void* target_addr, void* new_func, void** old_func) {
  if (!target_addr) return false;
  uintptr_t page_size = sysconf(_SC_PAGESIZE);
  uintptr_t addr = reinterpret_cast<uintptr_t>(target_addr);
  uintptr_t page_start = addr & ~(page_size - 1);

  if (mprotect(reinterpret_cast<void*>(page_start), page_size * 2, PROT_READ | PROT_WRITE) != 0) {
    LOGE("mprotect failed on GOT slot %p: %s", target_addr, strerror(errno));
    return false;
  }

  if (old_func && !*old_func) {
    *old_func = *reinterpret_cast<void**>(target_addr);
  }
  *reinterpret_cast<void**>(target_addr) = new_func;
  return true;
}

static void ScanLibraryGot(const struct dl_phdr_info* info, void**& out_got_dlopen, void**& out_got_dlclose) {
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
      if (strcmp(sym_name, "dlopen") == 0 && !out_got_dlopen) {
        out_got_dlopen = reinterpret_cast<void**>(base + r[i].r_offset);
      } else if (strcmp(sym_name, "dlclose") == 0 && !out_got_dlclose) {
        out_got_dlclose = reinterpret_cast<void**>(base + r[i].r_offset);
      }
    }
  };

  scan_relas(jmprel, pltrelsz);
  scan_relas(rela, relasz);
}

static int DlIteratePatchDlopenCallback(struct dl_phdr_info* info, size_t /*size*/, void* /*data*/) {
  if (!info || !info->dlpi_name || info->dlpi_name[0] == '\0') {
    return 0;
  }

  const char* name = info->dlpi_name;
  bool is_target = (strstr(name, "libdantes_inferno") != nullptr) ||
                   (strstr(name, "librexruntime") != nullptr) ||
                   (strstr(name, "librexgpu-xenos") != nullptr) ||
                   (strstr(name, "libSDL3") != nullptr);

  if (!is_target) {
    return 0;
  }

  void** got_dlopen = nullptr;
  void** got_dlclose = nullptr;
  ScanLibraryGot(info, got_dlopen, got_dlclose);

  if (got_dlopen) {
    void* old_func = nullptr;
    if (PatchGotSlot(got_dlopen, reinterpret_cast<void*>(HookedDlopen), &old_func)) {
      LOGI("Successfully hooked dlopen in %s (GOT slot: %p)", name, got_dlopen);
      if (!g_real_dlopen && old_func) {
        g_real_dlopen = reinterpret_cast<void*(*)(const char*, int)>(old_func);
      }
    }
  }

  if (got_dlclose) {
    void* old_func = nullptr;
    if (PatchGotSlot(got_dlclose, reinterpret_cast<void*>(HookedDlclose), &old_func)) {
      LOGI("Successfully hooked dlclose in %s (GOT slot: %p)", name, got_dlclose);
      if (!g_real_dlclose && old_func) {
        g_real_dlclose = reinterpret_cast<int(*)(void*)>(old_func);
      }
    }
  }

  return 0;
}

}  // namespace

void SetDriverConfig(const DriverConfig& config) {
  std::lock_guard<std::mutex> lock(g_driver_mutex);
  g_driver_config = config;
  LOGI("DriverConfig updated: use_turnip=%d, driver_dir='%s', driver_name='%s', turbo=%d",
       config.use_turnip, config.driver_dir.c_str(), config.driver_name.c_str(), config.enable_turbo);
}

const DriverConfig& GetDriverConfig() {
  std::lock_guard<std::mutex> lock(g_driver_mutex);
  return g_driver_config;
}

bool InitializeDriver() {
  std::lock_guard<std::mutex> lock(g_driver_mutex);

  if (!g_driver_config.use_turnip) {
    LOGI("Turnip driver is disabled. Using Android system Vulkan driver.");
    return false;
  }

  std::string driver_dir = g_driver_config.driver_dir;
  if (!driver_dir.empty() && driver_dir.back() != '/') {
    driver_dir += '/';
  }

  std::string driver_name = g_driver_config.driver_name;
  if (driver_name.empty()) {
    driver_name = "vulkan.adreno.so";
  }

  std::string hook_dir = g_driver_config.hook_lib_dir;
  std::string full_path = driver_dir + driver_name;

  LOGI("Attempting to load custom Turnip driver via AdrenoTools:");
  LOGI("  Driver path: %s", full_path.c_str());
  LOGI("  Hook directory: %s", hook_dir.c_str());

  std::error_code ec;
  if (!std::filesystem::exists(full_path, ec)) {
    LOGE("Custom driver file does not exist at '%s'! Falling back to system driver.", full_path.c_str());
    return false;
  }

  void* handle = adrenotools_open_libvulkan(
      RTLD_NOW,
      ADRENOTOOLS_DRIVER_CUSTOM,
      nullptr,                      // tmpLibDir
      hook_dir.c_str(),             // hookLibDir (nativeLibraryDir)
      driver_dir.c_str(),           // customDriverDir
      driver_name.c_str(),          // customDriverName
      nullptr,                      // fileRedirectDir
      nullptr                       // userMappingHandle
  );

  if (!handle) {
    LOGE("adrenotools_open_libvulkan returned NULL! Falling back to system Vulkan driver.");
    return false;
  }

  g_adrenotools_vulkan_handle = handle;
  LOGI("AdrenoTools Turnip driver opened successfully! Handle: %p", handle);

  if (!g_real_dlopen) {
    g_real_dlopen = reinterpret_cast<void*(*)(const char*, int)>(dlsym(RTLD_DEFAULT, "dlopen"));
  }

  // Patch dlopen across target libraries
  dl_iterate_phdr(DlIteratePatchDlopenCallback, nullptr);

  // Set SDL hint to ensure SDL3 requests libvulkan.so
  SDL_SetHint("SDL_VULKAN_LIBRARY", "libvulkan.so");

  if (g_driver_config.enable_turbo) {
    LOGI("Activating AdrenoTools GPU Turbo mode");
    adrenotools_set_turbo(true);
  }

  setenv("MESA_VK_WSI_PRESENT_MODE", "mailbox", 0);
  return true;
}

bool IsTurnipActive() {
  std::lock_guard<std::mutex> lock(g_driver_mutex);
  return g_adrenotools_vulkan_handle != nullptr;
}

void ShutdownDriver() {
  std::lock_guard<std::mutex> lock(g_driver_mutex);
  if (g_adrenotools_vulkan_handle) {
    LOGI("Shutting down AdrenoTools driver (%p)", g_adrenotools_vulkan_handle);
    void* handle = g_adrenotools_vulkan_handle;
    g_adrenotools_vulkan_handle = nullptr;
    if (!g_real_dlclose) {
      g_real_dlclose = reinterpret_cast<int(*)(void*)>(dlsym(RTLD_DEFAULT, "dlclose"));
    }
    if (g_real_dlclose) {
      g_real_dlclose(handle);
    }
  }
}

void LogTextureCompressionSupport() {
  if (g_driver_config.disable_debug) {
    return;
  }
  void* vk_lib = dlopen("libvulkan.so", RTLD_NOW);
  if (!vk_lib) {
    LOGW("Failed to dlopen libvulkan.so for caps check: %s", dlerror());
    return;
  }

  auto pfn_vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(vk_lib, "vkGetInstanceProcAddr"));
  if (!pfn_vkGetInstanceProcAddr) {
    LOGW("Failed to dlsym vkGetInstanceProcAddr");
    if (vk_lib != g_adrenotools_vulkan_handle && g_real_dlclose) g_real_dlclose(vk_lib);
    return;
  }

  auto pfn_vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(pfn_vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
  if (!pfn_vkCreateInstance) {
    LOGW("Failed to resolve vkCreateInstance");
    if (vk_lib != g_adrenotools_vulkan_handle && g_real_dlclose) g_real_dlclose(vk_lib);
    return;
  }

  VkApplicationInfo app_info{};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "DantesGpuCaps";
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = "None";
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.apiVersion = VK_API_VERSION_1_0;

  VkInstanceCreateInfo inst_info{};
  inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  inst_info.pApplicationInfo = &app_info;

  VkInstance instance = VK_NULL_HANDLE;
  VkResult res = pfn_vkCreateInstance(&inst_info, nullptr, &instance);
  if (res != VK_SUCCESS || instance == VK_NULL_HANDLE) {
    LOGW("vkCreateInstance failed: %d", res);
    if (vk_lib != g_adrenotools_vulkan_handle && g_real_dlclose) g_real_dlclose(vk_lib);
    return;
  }

  auto pfn_vkEnumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(pfn_vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices"));
  auto pfn_vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(pfn_vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties"));
  auto pfn_vkGetPhysicalDeviceFeatures = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(pfn_vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures"));
  auto pfn_vkDestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(pfn_vkGetInstanceProcAddr(instance, "vkDestroyInstance"));

  if (pfn_vkEnumeratePhysicalDevices && pfn_vkGetPhysicalDeviceProperties && pfn_vkGetPhysicalDeviceFeatures) {
    uint32_t device_count = 0;
    pfn_vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    if (device_count > 0) {
      std::vector<VkPhysicalDevice> devices(device_count);
      pfn_vkEnumeratePhysicalDevices(instance, &device_count, devices.data());

      for (uint32_t i = 0; i < device_count; ++i) {
        VkPhysicalDeviceProperties props{};
        pfn_vkGetPhysicalDeviceProperties(devices[i], &props);

        VkPhysicalDeviceFeatures features{};
        pfn_vkGetPhysicalDeviceFeatures(devices[i], &features);

        LOGI("=== GPU Device %u: %s (Driver Version: 0x%x, API: %u.%u.%u) ===",
             i, props.deviceName, props.driverVersion,
             VK_VERSION_MAJOR(props.apiVersion),
             VK_VERSION_MINOR(props.apiVersion),
             VK_VERSION_PATCH(props.apiVersion));
        LOGI("  textureCompressionBC (DXT1/3/5): %s",
             features.textureCompressionBC ? "SUPPORTED (1)" : "NOT SUPPORTED (0)");
        LOGI("  textureCompressionETC2: %s",
             features.textureCompressionETC2 ? "SUPPORTED (1)" : "NOT SUPPORTED (0)");
        LOGI("  textureCompressionASTC_LDR: %s",
             features.textureCompressionASTC_LDR ? "SUPPORTED (1)" : "NOT SUPPORTED (0)");

        if (!features.textureCompressionBC) {
          LOGW("CRITICAL WARNING: Driver does NOT report BC texture compression! "
               "Xbox 360 assets rely on BC1/BC2/BC3 (DXT1/3/5).");
        } else {
          LOGI("SUCCESS: Driver reports native hardware support for BC texture compression.");
        }
      }
    }
  }

  if (pfn_vkDestroyInstance) {
    pfn_vkDestroyInstance(instance, nullptr);
  }
  if (vk_lib != g_adrenotools_vulkan_handle && g_real_dlclose) {
    g_real_dlclose(vk_lib);
  }
}

}  // namespace dantes::driver

extern "C" JNIEXPORT void JNICALL
Java_com_dantesinferno_game_MainActivity_nativeSetDriverConfig(
    JNIEnv* env, jobject /*thiz*/,
    jstring driverDir, jstring driverName, jstring hookLibDir,
    jboolean useTurnip, jboolean enableTurbo, jboolean disableDebug) {
  dantes::driver::DriverConfig cfg;
  cfg.use_turnip = useTurnip;
  cfg.enable_turbo = enableTurbo;
  cfg.disable_debug = disableDebug;

  if (driverDir) {
    const char* c_dir = env->GetStringUTFChars(driverDir, nullptr);
    if (c_dir) {
      cfg.driver_dir = c_dir;
      env->ReleaseStringUTFChars(driverDir, c_dir);
    }
  }
  if (driverName) {
    const char* c_name = env->GetStringUTFChars(driverName, nullptr);
    if (c_name) {
      cfg.driver_name = c_name;
      env->ReleaseStringUTFChars(driverName, c_name);
    }
  }
  if (hookLibDir) {
    const char* c_hook = env->GetStringUTFChars(hookLibDir, nullptr);
    if (c_hook) {
      cfg.hook_lib_dir = c_hook;
      env->ReleaseStringUTFChars(hookLibDir, c_hook);
    }
  }

  dantes::driver::SetDriverConfig(cfg);
}

#else

namespace dantes::driver {

void SetDriverConfig(const DriverConfig&) {}
const DriverConfig& GetDriverConfig() { static DriverConfig cfg; return cfg; }
bool InitializeDriver() { return false; }
bool IsTurnipActive() { return false; }
void ShutdownDriver() {}
void LogTextureCompressionSupport() {}

}  // namespace dantes::driver

#endif
