#pragma once

#include <string>

namespace dantes::driver {

struct DriverConfig {
  bool use_turnip = false;
  std::string driver_dir;      // Directory containing custom driver (e.g. /data/user/0/com.dantesinferno.game/files/custom_driver/)
  std::string driver_name;     // Soname of the driver (e.g. vulkan.adreno.so)
  std::string hook_lib_dir;    // Path to app's nativeLibraryDir
  bool enable_turbo = true;    // Force maximum Adreno GPU clocks
  bool disable_debug = true;   // Disable all debug logging
};

// Sets driver configuration (called via JNI from MainActivity)
void SetDriverConfig(const DriverConfig& config);

// Gets current driver configuration
const DriverConfig& GetDriverConfig();

// Initializes AdrenoTools Turnip driver and installs dlopen interception
bool InitializeDriver();

// Checks if Turnip driver is currently loaded and active
bool IsTurnipActive();

// Cleanup on shutdown
void ShutdownDriver();

// Diagnostic only: creates a throwaway VkInstance to log GPU caps (BC texture support)
void LogTextureCompressionSupport();

}  // namespace dantes::driver
