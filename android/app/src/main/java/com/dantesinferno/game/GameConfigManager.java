package com.dantesinferno.game;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

import java.io.File;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class GameConfigManager {

    private static final String TAG = "GameConfigManager";

    public static final String PREF_NAME = "dantes_settings";
    public static final String PREF_USE_TURNIP = "use_turnip";
    public static final String PREF_DRIVER_NAME = "driver_name";
    public static final String PREF_TURBO = "turbo_mode";
    public static final String PREF_DISABLE_DEBUG = "disable_debug";
    public static final String PREF_CUSTOM_GAME_PATH = "custom_game_path";

    public static final String DEFAULT_DRIVER_NAME = "vulkan.adreno.so";
    public static final String EXTRA_ISO_PATH = "EXTRA_ISO_PATH";

    public static final String PREF_TURNIP_IN_FLIGHT = "turnip_in_flight";
    public static final String PREF_DRIVER_MIGRATED_V2 = "driver_preference_migrated_v2";

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

    public static boolean isTurnipEnabled(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);
        return prefs.getBoolean(PREF_USE_TURNIP, false);
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
            return "Turnip AdrenoTools (" + getDriverName(context) + ") [ATIVO]";
        } else if (useTurnip && !hasDriver) {
            return "Turnip ativado (Nenhum driver .zip instalado ainda)";
        } else {
            return "Qualcomm OEM (Driver do Sistema Vulkan)";
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

        if (resolvedLibraryName == null) {
            File[] files = customDir.listFiles((dir, name) -> name.endsWith(".so"));
            if (files != null && files.length > 0) {
                resolvedLibraryName = files[0].getName();
            }
        }

        if (resolvedLibraryName != null) {
            setTurnipEnabled(context, true);
            setDriverName(context, resolvedLibraryName);
        }

        return resolvedLibraryName;
    }
}
