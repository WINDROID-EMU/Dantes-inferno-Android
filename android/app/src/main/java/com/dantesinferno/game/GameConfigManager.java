package com.dantesinferno.game;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;

public class GameConfigManager {

    private static final String TAG = "GameConfigManager";

    public static final String PREF_NAME = "dantes_settings";

    // Driver preferences
    public static final String PREF_USE_TURNIP = "use_turnip";
    public static final String PREF_DRIVER_NAME = "driver_name";
    public static final String PREF_TURBO = "turbo_mode";
    public static final String PREF_DISABLE_DEBUG = "disable_debug";
    public static final String PREF_A6XX_COMPAT = "a6xx_compat_mode";
    public static final String PREF_CUSTOM_GAME_PATH = "custom_game_path";

    public static final String DEFAULT_DRIVER_NAME = "vulkan.adreno.so";
    public static final String EXTRA_ISO_PATH = "EXTRA_ISO_PATH";

    public static final String PREF_TURNIP_IN_FLIGHT = "turnip_in_flight";
    public static final String PREF_DRIVER_MIGRATED_V2 = "driver_preference_migrated_v2";

    // Graphics & Engine preferences
    public static final String PREF_RES_SCALE_IDX = "resolution_scale_idx";
    public static final String PREF_VSYNC = "vsync_enabled";
    public static final String PREF_PRESENT_EFFECT_IDX = "present_effect_idx";
    public static final String PREF_ANISOTROPIC_IDX = "anisotropic_idx";
    public static final String PREF_VULKAN_PRESENT_MODE_IDX = "vulkan_present_mode_idx";
    public static final String PREF_ASYNC_SHADERS = "async_shaders_enabled";
    public static final String PREF_PIPELINE_THREADS_IDX = "pipeline_threads_idx";

    // Virtual Controller preferences
    public static final String PREF_SHOW_VIRTUAL_CONTROLLER = "show_virtual_controller";
    public static final String PREF_CONTROLLER_OPACITY = "controller_opacity";
    public static final String PREF_SHOW_FPS = "show_fps_hud";
    public static final String PREF_FPS_OPACITY = "fps_hud_opacity";

    public static File getStorageDir(Context context) {
        File ext = context.getExternalFilesDir(null);
        return (ext != null) ? ext : context.getFilesDir();
    }

    public static File getGameDir(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);
        String custom = prefs.getString(PREF_CUSTOM_GAME_PATH, null);
        if (custom != null && !custom.isEmpty()) {
            File f = new File(custom);
            if (f.exists()) return f;
        }

        File[] candidates = new File[] {
            new File("/sdcard/DantesInferno/game"),
            new File("/sdcard/DantesInferno"),
            new File("/storage/emulated/0/DantesInferno/game"),
            new File("/storage/emulated/0/DantesInferno"),
            new File(getStorageDir(context), "game")
        };

        for (File dir : candidates) {
            if (dir.exists() && new File(dir, "default.xex").exists()) {
                return dir;
            }
        }

        return new File(getStorageDir(context), "game");
    }

    public static File getCacheDir(Context context) {
        return new File(getStorageDir(context), "cache");
    }

    public static long getCacheSizeBytes(Context context) {
        File cacheDir = getCacheDir(context);
        return calculateDirectorySize(cacheDir);
    }

    private static long calculateDirectorySize(File dir) {
        if (dir == null || !dir.exists()) return 0;
        long size = 0;
        File[] files = dir.listFiles();
        if (files != null) {
            for (File file : files) {
                if (file.isDirectory()) {
                    size += calculateDirectorySize(file);
                } else {
                    size += file.length();
                }
            }
        }
        return size;
    }

    public static boolean clearCache(Context context) {
        File cacheDir = getCacheDir(context);
        if (!cacheDir.exists()) return true;
        return deleteDirectoryContents(cacheDir);
    }

    private static boolean deleteDirectoryContents(File dir) {
        if (dir == null || !dir.exists()) return true;
        boolean success = true;
        File[] files = dir.listFiles();
        if (files != null) {
            for (File file : files) {
                if (file.isDirectory()) {
                    success &= deleteDirectoryContents(file);
                    file.delete();
                } else {
                    success &= file.delete();
                }
            }
        }
        return success;
    }

    public static File getDefaultXexFile(Context context) {
        return new File(getGameDir(context), "default.xex");
    }

    public static boolean isGameInstalled(Context context) {
        File xex = getDefaultXexFile(context);
        return xex.exists() && xex.isFile() && xex.length() > 0;
    }

    public static File getCustomDriverDir(Context context) {
        File dir = new File(context.getFilesDir(), "custom_driver");
        if (!dir.exists()) {
            dir.mkdirs();
        }
        return dir;
    }

    // Driver accessors
    public static boolean isTurnipEnabled(Context context) {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
                      .getBoolean(PREF_USE_TURNIP, false);
    }

    public static void setTurnipEnabled(Context context, boolean enabled) {
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
               .edit()
               .putBoolean(PREF_USE_TURNIP, enabled)
               .apply();
    }

    public static void ensureDriverPreferencesMigrated(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);
        if (!prefs.getBoolean(PREF_DRIVER_MIGRATED_V2, false)) {
            if (!prefs.contains(PREF_USE_TURNIP)) {
                prefs.edit().putBoolean(PREF_USE_TURNIP, false).apply();
            }
            prefs.edit().putBoolean(PREF_DRIVER_MIGRATED_V2, true).apply();
        }
    }

    public static boolean checkAndRecoverTurnipCrash(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);
        boolean inFlight = prefs.getBoolean(PREF_TURNIP_IN_FLIGHT, false);
        if (inFlight) {
            prefs.edit()
                 .putBoolean(PREF_TURNIP_IN_FLIGHT, false)
                 .putBoolean(PREF_USE_TURNIP, false)
                 .apply();
            Log.w(TAG, "Turnip launch in flight flag was set. Reset to system driver.");
            return true;
        }
        return false;
    }

    public static void markTurnipLaunchInProgress(Context context, boolean inProgress) {
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
               .edit()
               .putBoolean(PREF_TURNIP_IN_FLIGHT, inProgress)
               .apply();
    }

    public static boolean isDisableDebug(Context context) {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
                      .getBoolean(PREF_DISABLE_DEBUG, true);
    }

    public static void setDisableDebug(Context context, boolean disabled) {
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
               .edit()
               .putBoolean(PREF_DISABLE_DEBUG, disabled)
               .apply();
    }

    public static boolean isTurboEnabled(Context context) {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
                      .getBoolean(PREF_TURBO, true);
    }

    public static void setTurboEnabled(Context context, boolean enabled) {
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
               .edit()
               .putBoolean(PREF_TURBO, enabled)
               .apply();
    }

    public static boolean isAdreno6xxHardware() {
        String hardware = android.os.Build.HARDWARE != null ? android.os.Build.HARDWARE.toLowerCase() : "";
        String board = android.os.Build.BOARD != null ? android.os.Build.BOARD.toLowerCase() : "";
        String device = android.os.Build.DEVICE != null ? android.os.Build.DEVICE.toLowerCase() : "";
        String model = android.os.Build.MODEL != null ? android.os.Build.MODEL.toLowerCase() : "";

        // Qualcomm Snapdragon 865/865+/870 (kona), SD855 (msmnile), SD888 (lahaina), SD778G (yupik), SD765G (lito)
        if (hardware.contains("kona") || board.contains("kona") || device.contains("nio") ||
            hardware.contains("lahaina") || board.contains("lahaina") ||
            hardware.contains("msmnile") || board.contains("msmnile") ||
            hardware.contains("yupik") || board.contains("yupik") ||
            hardware.contains("lito") || board.contains("lito") ||
            model.contains("g(100)") || model.contains("g100")) {
            return true;
        }

        if (android.os.Build.VERSION.SDK_INT >= 31) {
            String soc = android.os.Build.SOC_MODEL != null ? android.os.Build.SOC_MODEL.toUpperCase() : "";
            if (soc.contains("8250") || soc.contains("8150") || soc.contains("8350") || soc.contains("7325") || soc.contains("7250")) {
                return true;
            }
        }
        return false;
    }

    public static boolean isA6xxCompatEnabled(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);
        // Default to false: modern Turnip drivers (T30+, Kimchi R18) have full GMEM tiling support on Adreno 650.
        // Forcing sysmem breaks FBO framebuffer resolves and causes a black screen.
        return prefs.getBoolean(PREF_A6XX_COMPAT, false);
    }

    public static void setA6xxCompatEnabled(Context context, boolean enabled) {
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
               .edit()
               .putBoolean(PREF_A6XX_COMPAT, enabled)
               .apply();
    }

    public static String getDriverName(Context context) {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
                      .getString(PREF_DRIVER_NAME, DEFAULT_DRIVER_NAME);
    }

    public static void setDriverName(Context context, String name) {
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
               .edit()
               .putString(PREF_DRIVER_NAME, name)
               .apply();
    }

    public static boolean hasCustomDriverInstalled(Context context) {
        File customDir = getCustomDriverDir(context);
        String name = getDriverName(context);
        File driverFile = new File(customDir, name);
        if (driverFile.exists() && driverFile.length() > 0) {
            return true;
        }
        File[] files = customDir.listFiles((dir, fName) -> fName.endsWith(".so"));
        return (files != null && files.length > 0);
    }

    public static String getActiveDriverDescription(Context context) {
        boolean useTurnip = isTurnipEnabled(context);
        boolean hasDriver = hasCustomDriverInstalled(context);
        if (useTurnip && hasDriver) {
            String drvName = getDriverName(context);
            String extra = isA6xxCompatEnabled(context) ? " (noubwc ativo)" : "";
            if (drvName.contains("07") && (isAdreno6xxHardware() || isA6xxCompatEnabled(context))) {
                extra += " ⚠️ ALERTA: Driver A7xx em GPU A6xx!";
            }
            return "Turnip AdrenoTools (" + drvName + ")" + extra + " [ATIVO]";
        } else if (useTurnip && !hasDriver) {
            return "Turnip ativado (Nenhum driver .zip instalado ainda)";
        } else {
            return "Qualcomm OEM (Driver do Sistema Vulkan)";
        }
    }

    // Graphics Settings Accessors
    public static int getResolutionScaleIdx(Context context) {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
                      .getInt(PREF_RES_SCALE_IDX, 0); // 0 = 720p 1x
    }

    public static void setResolutionScaleIdx(Context context, int idx) {
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
               .edit().putInt(PREF_RES_SCALE_IDX, idx).apply();
    }

    public static int getResolutionScaleValue(Context context) {
        int idx = getResolutionScaleIdx(context);
        switch (idx) {
            case 1: // 1080p
            case 2: // 1440p
                return 2;
            case 0: // 720p
            case 3: // 540p
            default:
                return 1;
        }
    }

    public static boolean isVsyncEnabled(Context context) {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
                      .getBoolean(PREF_VSYNC, true);
    }

    public static void setVsyncEnabled(Context context, boolean enabled) {
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
               .edit().putBoolean(PREF_VSYNC, enabled).apply();
    }

    public static int getPresentEffectIdx(Context context) {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
                      .getInt(PREF_PRESENT_EFFECT_IDX, 1); // 1 = fxaa default
    }

    public static void setPresentEffectIdx(Context context, int idx) {
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
               .edit().putInt(PREF_PRESENT_EFFECT_IDX, idx).apply();
    }

    public static String getPresentEffectString(Context context) {
        int idx = getPresentEffectIdx(context);
        switch (idx) {
            case 0: return "none";
            case 1: return "fxaa";
            case 2: return "cas";
            case 3: return "fsr";
            default: return "fxaa";
        }
    }

    public static int getAnisotropicIdx(Context context) {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
                      .getInt(PREF_ANISOTROPIC_IDX, 0); // 0 = Desativado (0x) default for performance
    }

    public static void setAnisotropicIdx(Context context, int idx) {
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
               .edit().putInt(PREF_ANISOTROPIC_IDX, idx).apply();
    }

    public static int getAnisotropicValue(Context context) {
        int idx = getAnisotropicIdx(context);
        switch (idx) {
            case 0: return 0;  // Off / Bilinear (Maximum Performance)
            case 1: return 2;  // 2x (Balanced)
            case 2: return 3;  // 4x (Medium)
            case 3: return 5;  // 16x (High Quality)
            case 4:
            default:
                return -1; // Native / Game Default (-1)
        }
    }

    public static int getVulkanPresentModeIdx(Context context) {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
                      .getInt(PREF_VULKAN_PRESENT_MODE_IDX, 0); // 0 = FIFO (VSync)
    }

    public static void setVulkanPresentModeIdx(Context context, int idx) {
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
               .edit().putInt(PREF_VULKAN_PRESENT_MODE_IDX, idx).apply();
    }

    public static boolean isAsyncShadersEnabled(Context context) {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
                      .getBoolean(PREF_ASYNC_SHADERS, true);
    }

    public static void setAsyncShadersEnabled(Context context, boolean enabled) {
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
               .edit().putBoolean(PREF_ASYNC_SHADERS, enabled).apply();
    }

    public static int getPipelineThreadsIdx(Context context) {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
                      .getInt(PREF_PIPELINE_THREADS_IDX, 0); // 0 = Auto (-1)
    }

    public static void setPipelineThreadsIdx(Context context, int idx) {
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
               .edit().putInt(PREF_PIPELINE_THREADS_IDX, idx).apply();
    }

    public static int getPipelineThreadsValue(Context context) {
        int idx = getPipelineThreadsIdx(context);
        switch (idx) {
            case 1: return 2;
            case 2: return 4;
            case 3: return 1;
            case 0:
            default:
                return -1; // Auto
        }
    }

    public static boolean isShowVirtualController(Context context) {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
                      .getBoolean(PREF_SHOW_VIRTUAL_CONTROLLER, true);
    }

    public static void setShowVirtualController(Context context, boolean show) {
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
               .edit().putBoolean(PREF_SHOW_VIRTUAL_CONTROLLER, show).apply();
    }

    public static int getControllerOpacity(Context context) {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
                      .getInt(PREF_CONTROLLER_OPACITY, 70);
    }

    public static void setControllerOpacity(Context context, int opacity) {
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
               .edit().putInt(PREF_CONTROLLER_OPACITY, opacity).apply();
    }

    public static boolean isShowFpsEnabled(Context context) {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
                      .getBoolean(PREF_SHOW_FPS, true);
    }

    public static void setShowFpsEnabled(Context context, boolean show) {
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
               .edit().putBoolean(PREF_SHOW_FPS, show).apply();
    }

    public static int getFpsOpacity(Context context) {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
                      .getInt(PREF_FPS_OPACITY, 90);
    }

    public static void setFpsOpacity(Context context, int opacity) {
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
               .edit().putInt(PREF_FPS_OPACITY, opacity).apply();
    }

    /**
     * Writes dantes_inferno.toml to ensure settings are persistently loaded by rex::cvar::LoadConfig
     */
    public static void saveTomlConfig(Context context) {
        File storage = getStorageDir(context);
        if (storage == null) return;

        File tomlFile = new File(storage, "dantes_inferno.toml");
        StringBuilder sb = new StringBuilder();
        sb.append("# Dante's Inferno (ReXGlue) Configuration\n");
        sb.append("# Generated by Dante's Inferno Android Settings\n\n");

        int resScale = getResolutionScaleValue(context);
        boolean vsync = isVsyncEnabled(context);
        String effect = getPresentEffectString(context);
        int aniso = getAnisotropicValue(context);
        boolean asyncShaders = isAsyncShadersEnabled(context);
        int threads = getPipelineThreadsValue(context);
        int presentMode = getVulkanPresentModeIdx(context);

        sb.append("resolution_scale = ").append(resScale).append("\n");
        sb.append("draw_resolution_scale_x = ").append(resScale).append("\n");
        sb.append("draw_resolution_scale_y = ").append(resScale).append("\n");
        sb.append("vsync = ").append(vsync ? "true" : "false").append("\n");
        sb.append("swap_post_effect = \"").append(effect).append("\"\n");
        sb.append("anisotropic_override = ").append(aniso).append("\n");
        sb.append("async_shader_compilation = ").append(asyncShaders ? "true" : "false").append("\n");
        sb.append("vulkan_pipeline_creation_threads = ").append(threads).append("\n");
        sb.append("audio_maxqframes = 128\n");
        sb.append("ignore_thread_affinities = true\n");
        sb.append("ignore_thread_priorities = true\n");

        if (presentMode == 1) {
            sb.append("vulkan_allow_present_mode_mailbox = true\n");
            sb.append("vulkan_allow_present_mode_immediate = false\n");
        } else if (presentMode == 2) {
            sb.append("vulkan_allow_present_mode_immediate = true\n");
            sb.append("vulkan_allow_present_mode_mailbox = false\n");
        } else {
            sb.append("vulkan_allow_present_mode_mailbox = false\n");
            sb.append("vulkan_allow_present_mode_immediate = false\n");
        }

        try (FileWriter fw = new FileWriter(tomlFile)) {
            fw.write(sb.toString());
            Log.i(TAG, "Saved TOML config to: " + tomlFile.getAbsolutePath());
        } catch (IOException e) {
            Log.e(TAG, "Failed to write dantes_inferno.toml: " + e.getMessage(), e);
        }

        // Also mirror to game subfolder if it exists
        File gameFolder = new File(storage, "game");
        if (gameFolder.exists() && gameFolder.isDirectory()) {
            File gameToml = new File(gameFolder, "dantes_inferno.toml");
            try (FileWriter fw = new FileWriter(gameToml)) {
                fw.write(sb.toString());
            } catch (IOException ignored) {}
        }
    }

    public static String extractDriverZip(Context context, android.net.Uri uri) throws Exception {
        File customDir = getCustomDriverDir(context);
        File[] oldFiles = customDir.listFiles();
        if (oldFiles != null) {
            for (File f : oldFiles) f.delete();
        }

        String resolvedLibraryName = null;

        try (java.io.InputStream rawIn = context.getContentResolver().openInputStream(uri);
             java.io.BufferedInputStream bufIn = new java.io.BufferedInputStream(rawIn);
             java.util.zip.ZipInputStream zipIn = new java.util.zip.ZipInputStream(bufIn)) {

            java.util.zip.ZipEntry entry;
            byte[] buffer = new byte[64 * 1024];

            while ((entry = zipIn.getNextEntry()) != null) {
                String name = entry.getName();
                if (entry.isDirectory() || name.contains("..")) {
                    zipIn.closeEntry();
                    continue;
                }

                String simpleName = new File(name).getName();
                File outFile = new File(customDir, simpleName);

                if ("meta.json".equalsIgnoreCase(simpleName)) {
                    java.io.ByteArrayOutputStream baos = new java.io.ByteArrayOutputStream();
                    int len;
                    while ((len = zipIn.read(buffer)) > 0) {
                        baos.write(buffer, 0, len);
                    }
                    byte[] jsonBytes = baos.toByteArray();
                    try (java.io.FileOutputStream fos = new java.io.FileOutputStream(outFile)) {
                        fos.write(jsonBytes);
                    }
                    try {
                        org.json.JSONObject json = new org.json.JSONObject(new String(jsonBytes, java.nio.charset.StandardCharsets.UTF_8));
                        resolvedLibraryName = json.optString("libraryName", null);
                    } catch (Exception e) {
                        Log.w(TAG, "Failed to parse meta.json: " + e.getMessage());
                    }
                } else {
                    try (java.io.FileOutputStream fos = new java.io.FileOutputStream(outFile)) {
                        int len;
                        while ((len = zipIn.read(buffer)) > 0) {
                            fos.write(buffer, 0, len);
                        }
                    }
                    if (simpleName.endsWith(".so") && resolvedLibraryName == null) {
                        if (simpleName.equals("vulkan.adreno.so") || simpleName.contains("turnip") || simpleName.contains("freedreno")) {
                            resolvedLibraryName = simpleName;
                        }
                    }
                }
                zipIn.closeEntry();
            }
        }

        resolvedLibraryName = resolveBestDriverFileName(context, customDir, resolvedLibraryName);

        if (resolvedLibraryName != null) {
            setTurnipEnabled(context, true);
            setDriverName(context, resolvedLibraryName);
        }

        return resolvedLibraryName;
    }

    public static String resolveBestDriverFileName(Context context, File customDir, String metaResolvedName) {
        File[] files = customDir.listFiles((dir, name) -> name.endsWith(".so"));
        if (files == null || files.length == 0) return metaResolvedName;

        boolean isA6xx = isA6xxCompatEnabled(context) || isAdreno6xxHardware();

        File a6xxFile = null;
        File a7xxFile = null;
        File freedrenoFile = null;
        File genericVulkanFile = null;

        for (File f : files) {
            String name = f.getName().toLowerCase();
            if (name.contains("ad06") || name.contains("a6xx") || name.contains("adreno6")) {
                a6xxFile = f;
            } else if (name.contains("ad07") || name.contains("a7xx") || name.contains("adreno7")) {
                a7xxFile = f;
            } else if (name.contains("freedreno") || name.contains("turnip") || name.contains("purple")) {
                freedrenoFile = f;
            } else if (name.equals("vulkan.adreno.so")) {
                genericVulkanFile = f;
            }
        }

        if (isA6xx && a6xxFile != null) {
            Log.i(TAG, "Driver auto-selector: Selected Adreno 6xx driver: " + a6xxFile.getName());
            return a6xxFile.getName();
        } else if (!isA6xx && a7xxFile != null) {
            Log.i(TAG, "Driver auto-selector: Selected Adreno 7xx driver: " + a7xxFile.getName());
            return a7xxFile.getName();
        }

        if (metaResolvedName != null) {
            File metaFile = new File(customDir, metaResolvedName);
            if (metaFile.exists()) {
                if (isA6xx && metaResolvedName.contains("07") && a6xxFile != null) {
                    Log.i(TAG, "Driver auto-selector: Overriding meta.json (" + metaResolvedName + ") with A6xx driver: " + a6xxFile.getName());
                    return a6xxFile.getName();
                }
                return metaResolvedName;
            }
        }

        if (freedrenoFile != null) return freedrenoFile.getName();
        if (genericVulkanFile != null) return genericVulkanFile.getName();
        return files[0].getName();
    }
}
