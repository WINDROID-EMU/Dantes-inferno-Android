package com.dantesinferno.game;

import android.Manifest;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.PowerManager;
import android.provider.Settings;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.File;

/**
 * Main Game Activity for Dante's Inferno on Android.
 * Extends SDLActivity to manage Vulkan presentation, audio, lifecycle,
 * and overlay the Virtual Controller layout directly on top of the game view.
 */
public class MainActivity extends SDLActivity {

    private static final String TAG = "DantesInferno";
    private static final int REQUEST_STORAGE_PERMISSION = 1001;
    private static final int REQUEST_FOLDER_PICKER = 1002;

    public static final String PREF_NAME = "dantes_settings";

    private VirtualControllerLayout mVirtualController;

    // Native JNI bridge
    public static native void setGameRootEnv(String path);
    public static native void nativeOnIsoPicked(String path);
    public static native void nativeSetDriverConfig(String driverDir, String driverName, String hookLibDir, boolean useTurnip, boolean enableTurbo, boolean disableDebug);
    public static native void nativeSetGraphicsConfig(int resScale, boolean vsync, String presentEffect, boolean asyncShaders, int pipelineThreads, int presentMode);

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "c++_shared",
            "rexruntimerd",
            "rexgpu-xenosrd",
            "dantes_inferno"
        };
    }

    @Override
    protected String getMainFunction() {
        return "SDL_main";
    }

    @Override
    protected String getMainSharedObject() {
        return getApplicationInfo().nativeLibraryDir + "/libdantes_inferno.so";
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Configure edge-to-edge display cutout before window creation
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
            getWindow().setAttributes(lp);
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }

        super.onCreate(savedInstanceState);

        // Force landscape sensor orientation
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);

        // Keep screen on during gameplay
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        // Immersive sticky fullscreen
        applyImmersiveStickyMode();

        // Enable Sustained Performance Mode to prevent thermal throttling clock drops
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
            if (pm != null && pm.isSustainedPerformanceModeSupported()) {
                getWindow().setSustainedPerformanceMode(true);
                Log.i(TAG, "Android Sustained Performance Mode activated");
            }
        }

        // Configure AdrenoTools Turnip / System GPU Driver
        initDriverConfiguration();

        // Consume ISO if passed from TitleActivity
        String extraIso = getIntent().getStringExtra(GameConfigManager.EXTRA_ISO_PATH);
        if (extraIso != null && !extraIso.isEmpty()) {
            Log.i(TAG, "Consuming pending ISO from Intent extra: " + extraIso);
            nativeOnIsoPicked(extraIso);
        }

        // Check storage permissions and locate game files
        checkStoragePermissions();

        // Attach Windroid-style Virtual Controller on-screen overlay
        setupVirtualController();
    }

    private void initDriverConfiguration() {
        GameConfigManager.ensureDriverPreferencesMigrated(this);

        SharedPreferences prefs = getSharedPreferences(PREF_NAME, MODE_PRIVATE);
        boolean useTurnip = prefs.getBoolean(GameConfigManager.PREF_USE_TURNIP, false);
        boolean turbo = prefs.getBoolean(GameConfigManager.PREF_TURBO, true);
        String driverName = prefs.getString(GameConfigManager.PREF_DRIVER_NAME, "vulkan.adreno.so");

        File customDriverDir = new File(getFilesDir(), "custom_driver");
        String hookLibDir = getApplicationInfo().nativeLibraryDir;

        File driverFile = new File(customDriverDir, driverName);
        if (!driverFile.exists() && customDriverDir.exists()) {
            File[] files = customDriverDir.listFiles((dir, name) -> name.endsWith(".so"));
            if (files != null && files.length > 0) {
                driverName = files[0].getName();
                driverFile = files[0];
                prefs.edit().putString(GameConfigManager.PREF_DRIVER_NAME, driverName).apply();
            }
        }

        boolean disableDebug = GameConfigManager.isDisableDebug(this);
        boolean hasCustomDriver = driverFile.exists();
        if (useTurnip && hasCustomDriver) {
            Log.i(TAG, "Configuring AdrenoTools Turnip driver: dir=" + customDriverDir.getAbsolutePath() + ", name=" + driverName);
            GameConfigManager.markTurnipLaunchInProgress(this, true);
            nativeSetDriverConfig(customDriverDir.getAbsolutePath(), driverName, hookLibDir, true, turbo, disableDebug);
        } else {
            Log.i(TAG, "Configuring System Vulkan driver");
            GameConfigManager.markTurnipLaunchInProgress(this, false);
            nativeSetDriverConfig("", "", hookLibDir, false, false, disableDebug);
        }

        // Apply graphics, upscaler, and Vulkan settings
        GameConfigManager.saveTomlConfig(this);
        int resScale = GameConfigManager.getResolutionScaleValue(this);
        boolean vsync = GameConfigManager.isVsyncEnabled(this);
        String presentEffect = GameConfigManager.getPresentEffectString(this);
        boolean asyncShaders = GameConfigManager.isAsyncShadersEnabled(this);
        int pipelineThreads = GameConfigManager.getPipelineThreadsValue(this);
        int presentMode = GameConfigManager.getVulkanPresentModeIdx(this);

        Log.i(TAG, String.format("Applying graphics config: resScale=%d, vsync=%b, effect=%s, asyncShaders=%b, threads=%d, presentMode=%d",
            resScale, vsync, presentEffect, asyncShaders, pipelineThreads, presentMode));

        nativeSetGraphicsConfig(resScale, vsync, presentEffect, asyncShaders, pipelineThreads, presentMode);
    }


    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyImmersiveStickyMode();
        }
    }

    private void applyImmersiveStickyMode() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            getWindow().setDecorFitsSystemWindows(false);
            android.view.WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(
                    android.view.WindowInsets.Type.statusBars() |
                    android.view.WindowInsets.Type.navigationBars()
                );
                controller.setSystemBarsBehavior(
                    android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                );
            }
        } else {
            @SuppressWarnings("deprecation")
            int flags = View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                      | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                      | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                      | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                      | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                      | View.SYSTEM_UI_FLAG_FULLSCREEN;
            getWindow().getDecorView().setSystemUiVisibility(flags);
        }
    }

    private void setupVirtualController() {
        SharedPreferences prefs = getSharedPreferences(PREF_NAME, MODE_PRIVATE);
        boolean showVirtualController = prefs.getBoolean("show_virtual_controller", true);

        if (showVirtualController && mLayout != null) {
            runOnUiThread(() -> {
                if (mVirtualController == null) {
                    mVirtualController = new VirtualControllerLayout(this);
                    android.widget.RelativeLayout.LayoutParams lp = new android.widget.RelativeLayout.LayoutParams(
                        android.widget.RelativeLayout.LayoutParams.MATCH_PARENT,
                        android.widget.RelativeLayout.LayoutParams.MATCH_PARENT
                    );
                    mLayout.addView(mVirtualController, lp);
                    Log.i(TAG, "Virtual controller layout (XML) attached to game layout successfully");
                }
            });
        }
    }

    private void checkStoragePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                new AlertDialog.Builder(this)
                    .setTitle("Acesso aos Arquivos do Jogo")
                    .setMessage("O Dante's Inferno precisa de acesso aos arquivos para ler a ISO extraída (~7.8GB) contendo default.xex.")
                    .setPositiveButton("Conceder", (dialog, which) -> {
                        try {
                            Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                            intent.setData(Uri.parse("package:" + getPackageName()));
                            startActivityForResult(intent, REQUEST_STORAGE_PERMISSION);
                        } catch (Exception e) {
                            Intent intent = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                            startActivityForResult(intent, REQUEST_STORAGE_PERMISSION);
                        }
                    })
                    .setNegativeButton("Cancelar", (dialog, which) -> {
                        Toast.makeText(this, "Permissão negada. O jogo pode não encontrar os dados.", Toast.LENGTH_LONG).show() ;
                    })
                    .setCancelable(false)
                    .show();
                return;
            }
        } else {
            if (checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(
                    new String[]{Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE},
                    REQUEST_STORAGE_PERMISSION
                );
                return;
            }
        }

        locateGameData();
    }

    private void locateGameData() {
        File[] candidates = new File[] {
            new File("/sdcard/DantesInferno/game"),
            new File("/sdcard/DantesInferno"),
            new File("/storage/emulated/0/DantesInferno/game"),
            new File("/storage/emulated/0/DantesInferno"),
            new File(getExternalFilesDir(null), "game")
        };

        File foundDir = null;
        for (File dir : candidates) {
            if (dir.exists() && new File(dir, "default.xex").exists()) {
                foundDir = dir;
                break;
            }
        }

        String extraIso = getIntent().getStringExtra(GameConfigManager.EXTRA_ISO_PATH);
        if (foundDir != null) {
            Log.i(TAG, "Found game data directory: " + foundDir.getAbsolutePath());
            setGameRootEnv(foundDir.getAbsolutePath());
        } else if (extraIso != null && !extraIso.isEmpty()) {
            File targetDir = new File(getExternalFilesDir(null), "game");
            Log.i(TAG, "No extracted game data yet; setting target game root for ISO installer: " + targetDir.getAbsolutePath());
            setGameRootEnv(targetDir.getAbsolutePath());
        } else {
            promptSelectGameDirectory();
        }
    }

    private void promptSelectGameDirectory() {
        new AlertDialog.Builder(this)
            .setTitle("Arquivos do Jogo")
            .setMessage("Coloque a pasta 'game' contendo 'default.xex' e 'bigfile0.viv' em /sdcard/DantesInferno/game, ou selecione a pasta onde os arquivos foram extraídos.")
            .setPositiveButton("Selecionar Pasta", (dialog, which) -> {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                startActivityForResult(intent, REQUEST_FOLDER_PICKER);
            })
            .setNegativeButton("Continuar", null)
            .setCancelable(false)
            .show();
    }

    @Override
    public void setRequestedOrientation(int requestedOrientation) {
        // Enforce sensor landscape and prevent SDL from triggering orientation changes or activity recreation
        super.setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_STORAGE_PERMISSION) {
            locateGameData();
        } else if (requestCode == REQUEST_FOLDER_PICKER && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            if (uri != null) {
                String path = uri.getPath();
                if (path != null) {
                    setGameRootEnv(path);
                }
            }
        }
    }
}
