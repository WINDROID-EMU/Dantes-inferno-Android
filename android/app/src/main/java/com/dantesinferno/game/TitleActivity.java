package com.dantesinferno.game;

import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;

import java.io.File;

public class TitleActivity extends AppCompatActivity {

    private static final String TAG = "TitleActivity";
    private static final int REQUEST_CODE_STORAGE = 1001;
    private static final int REQUEST_CODE_FOLDER = 1002;

    private View contentContainer;
    private View btnStartGame;
    private TextView tvStartButton;
    private TextView tvGameStatus;
    private ImageView ivStatusIcon;
    private View btnSettings;
    private Button btnChangeIso;
    private TextView tvDriverStatus;
    private View chipGpuDriver;
    private FrameLayout layoutLoading;
    private TextView tvLoadingText;
    private View fadeOverlay;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Edge-to-edge behind cutout
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

        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        setContentView(R.layout.activity_title);

        hideSystemUi();
        checkStoragePermissions();
        initViews();
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemUi();

        GameConfigManager.ensureDriverPreferencesMigrated(this);
        if (GameConfigManager.checkAndRecoverTurnipCrash(this)) {
            new AlertDialog.Builder(this)
                .setTitle("Recuperação de Inicialização")
                .setMessage("O driver customizado foi restaurado para o driver do sistema para garantir estabilidade.")
                .setPositiveButton("OK", null)
                .show();
        }

        updateUiState();

        if (fadeOverlay != null && fadeOverlay.getVisibility() == View.VISIBLE) {
            fadeOverlay.animate()
                .alpha(0f)
                .setDuration(500)
                .withEndAction(() -> {
                    fadeOverlay.setVisibility(View.GONE);
                    if (btnStartGame != null) btnStartGame.setEnabled(true);
                })
                .start();
        } else if (btnStartGame != null) {
            btnStartGame.setEnabled(true);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemUi();
        }
    }

    private void hideSystemUi() {
        Window window = getWindow();
        if (window == null) return;
        try {
            View decorView = window.getDecorView();
            if (decorView != null) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    window.setDecorFitsSystemWindows(false);
                    WindowInsetsController controller = decorView.getWindowInsetsController();
                    if (controller != null) {
                        controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                        controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                    }
                } else {
                    @SuppressWarnings("deprecation")
                    int flags = View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                              | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                              | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                              | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                              | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                              | View.SYSTEM_UI_FLAG_FULLSCREEN;
                    decorView.setSystemUiVisibility(flags);
                }
            }
        } catch (Throwable ignored) {}
    }

    private void initViews() {
        contentContainer = findViewById(R.id.content_container);
        btnStartGame = findViewById(R.id.btn_start_game);
        tvStartButton = findViewById(R.id.tv_start_button);
        tvGameStatus = findViewById(R.id.tv_game_status);
        ivStatusIcon = findViewById(R.id.iv_status_icon);
        btnSettings = findViewById(R.id.btn_settings);
        btnChangeIso = findViewById(R.id.btn_change_iso);
        chipGpuDriver = findViewById(R.id.chip_gpu_driver);
        tvDriverStatus = findViewById(R.id.tv_driver_status);
        layoutLoading = findViewById(R.id.layout_loading);
        tvLoadingText = findViewById(R.id.tv_loading_text);
        fadeOverlay = findViewById(R.id.fade_overlay);

        // Safe-area insets
        if (contentContainer != null) {
            ViewCompat.setOnApplyWindowInsetsListener(contentContainer, (v, windowInsets) -> {
                Insets insets = windowInsets.getInsets(
                    WindowInsetsCompat.Type.displayCutout() | WindowInsetsCompat.Type.systemBars()
                );
                int baseH = (int) (24 * getResources().getDisplayMetrics().density);
                int baseV = (int) (16 * getResources().getDisplayMetrics().density);
                v.setPadding(
                    Math.max(baseH, insets.left),
                    Math.max(baseV, insets.top),
                    Math.max(baseH, insets.right),
                    Math.max(baseV, insets.bottom)
                );
                return WindowInsetsCompat.CONSUMED;
            });
        }

        btnStartGame.setOnClickListener(v -> onStartGameClicked());

        btnSettings.setOnClickListener(v -> {
            Intent intent = new Intent(TitleActivity.this, SettingsActivity.class);
            startActivity(intent);
        });

        if (btnChangeIso != null) {
            btnChangeIso.setVisibility(View.VISIBLE);
            btnChangeIso.setText("Selecionar Pasta...");
            btnChangeIso.setOnClickListener(v -> {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                startActivityForResult(intent, REQUEST_CODE_FOLDER);
            });
        }

        if (chipGpuDriver != null) {
            chipGpuDriver.setOnClickListener(v -> {
                Intent intent = new Intent(TitleActivity.this, SettingsActivity.class);
                startActivity(intent);
            });
        }
    }

    private void updateUiState() {
        boolean gameReady = GameConfigManager.isGameInstalled(this);

        if (gameReady) {
            tvStartButton.setText("INICIAR JOGO");
            tvGameStatus.setText("Dados do Dante's Inferno prontos (" + GameConfigManager.getDefaultXexFile(this).getParent() + ")");
            tvGameStatus.setTextColor(0xFFC9D1D9);
            ivStatusIcon.setImageResource(R.drawable.ic_check);
        } else {
            tvStartButton.setText("SELECIONAR DADOS");
            tvGameStatus.setText("default.xex não encontrado. Toque para selecionar a pasta.");
            tvGameStatus.setTextColor(0xFFFFA657);
            ivStatusIcon.setImageResource(R.drawable.ic_folder);
        }

        boolean turnip = GameConfigManager.isTurnipEnabled(this);
        if (chipGpuDriver != null) {
            chipGpuDriver.setVisibility(View.VISIBLE);
            if (turnip) {
                tvDriverStatus.setText("GPU: Driver Turnip");
                tvDriverStatus.setTextColor(0xFF7EE787);
            } else {
                tvDriverStatus.setText("GPU: Driver Sistema Vulkan");
                tvDriverStatus.setTextColor(0xFF58A6FF);
            }
        }
    }

    private void onStartGameClicked() {
        if (!GameConfigManager.isGameInstalled(this)) {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            startActivityForResult(intent, REQUEST_CODE_FOLDER);
            return;
        }

        btnStartGame.setEnabled(false);

        if (fadeOverlay != null) {
            fadeOverlay.setVisibility(View.VISIBLE);
            fadeOverlay.setAlpha(0f);
            fadeOverlay.animate()
                .alpha(1f)
                .setDuration(400)
                .withEndAction(this::launchGame)
                .start();
        } else {
            launchGame();
        }
    }

    private void launchGame() {
        Intent intent = new Intent(TitleActivity.this, MainActivity.class);
        startActivity(intent);
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
                            startActivityForResult(intent, REQUEST_CODE_STORAGE);
                        } catch (Exception e) {
                            Intent intent = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                            startActivityForResult(intent, REQUEST_CODE_STORAGE);
                        }
                    })
                    .setNegativeButton("Continuar", null)
                    .show();
            }
        } else {
            if (checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(
                    new String[]{Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE},
                    REQUEST_CODE_STORAGE
                );
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_CODE_FOLDER && resultCode == RESULT_OK && data != null) {
            Uri treeUri = data.getData();
            if (treeUri != null) {
                String path = treeUri.getPath();
                if (path != null) {
                    getSharedPreferences(GameConfigManager.PREF_NAME, MODE_PRIVATE)
                        .edit()
                        .putString(GameConfigManager.PREF_CUSTOM_GAME_PATH, path)
                        .apply();
                }
                updateUiState();
            }
        }
    }
}
