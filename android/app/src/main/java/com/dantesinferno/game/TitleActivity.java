package com.dantesinferno.game;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
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
import java.io.FileOutputStream;
import java.io.InputStream;

public class TitleActivity extends AppCompatActivity {

    private static final String TAG = "TitleActivity";
    private static final int REQUEST_CODE_STORAGE = 1001;
    private static final int REQUEST_CODE_ISO = 1002;

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
        AppUpdater.checkAndPromptUpdate(this, false);
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
            btnChangeIso.setOnClickListener(v -> launchIsoPicker());
        }

        if (chipGpuDriver != null) {
            chipGpuDriver.setOnClickListener(v -> {
                Intent intent = new Intent(TitleActivity.this, SettingsActivity.class);
                startActivity(intent);
            });
        }

        updateUiState();
    }

    private void updateUiState() {
        boolean gameReady = GameConfigManager.isGameInstalled(this);

        if (gameReady) {
            tvStartButton.setText("INICIAR JOGO");
            tvGameStatus.setText("Dados do Dante's Inferno prontos para iniciar");
            tvGameStatus.setTextColor(0xFF7EE787);
            ivStatusIcon.setImageResource(R.drawable.ic_check);
            ivStatusIcon.setColorFilter(0xFF7EE787);
            if (btnChangeIso != null) {
                btnChangeIso.setVisibility(View.VISIBLE);
                btnChangeIso.setText("Trocar / Reinstalar ISO...");
            }
        } else {
            tvStartButton.setText("SELECIONAR ISO E INICIAR");
            tvGameStatus.setText("Nenhuma ISO carregada (Toque para selecionar a ISO do jogo)");
            tvGameStatus.setTextColor(0xFFFFA657);
            ivStatusIcon.setImageResource(R.drawable.ic_folder);
            ivStatusIcon.setColorFilter(0xFFFFA657);
            if (btnChangeIso != null) {
                btnChangeIso.setVisibility(View.GONE);
            }
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
            launchIsoPicker();
            return;
        }

        launchGame(null);
    }

    private void launchIsoPicker() {
        Toast.makeText(this, "Selecione o arquivo ISO do Dante's Inferno (Xbox 360)", Toast.LENGTH_LONG).show();
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQUEST_CODE_ISO);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_CODE_ISO) {
            if (resultCode == Activity.RESULT_OK && data != null && data.getData() != null) {
                handlePickedIsoUri(data.getData());
            } else {
                Toast.makeText(this, "Seleção de ISO cancelada.", Toast.LENGTH_SHORT).show();
            }
        }
    }

    private void handlePickedIsoUri(Uri uri) {
        if (layoutLoading != null) {
            layoutLoading.setVisibility(View.VISIBLE);
            if (tvLoadingText != null) {
                tvLoadingText.setText("Processando imagem ISO selecionada... Aguarde.");
            }
        }

        new Thread(() -> {
            try {
                String fileName = getFileName(uri);
                if (fileName == null || fileName.isEmpty()) {
                    fileName = "dantes_inferno.iso";
                }

                // If file is directly on disk
                if ("file".equalsIgnoreCase(uri.getScheme()) && uri.getPath() != null) {
                    String path = uri.getPath();
                    runOnUiThread(() -> {
                        if (layoutLoading != null) layoutLoading.setVisibility(View.GONE);
                        launchGame(path);
                    });
                    return;
                }

                // Instant ISO attachment via file descriptor detachment:
                // detachFd() hands the open Linux file descriptor to the process without closing it.
                // It remains accessible to the native engine via /proc/self/fd/<fd>, avoiding
                // copying 7.8 GB of data to disk and saving minutes of setup time and storage.
                try {
                    ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(uri, "r");
                    if (pfd != null) {
                        int fd = pfd.detachFd();
                        if (fd >= 0) {
                            final String procFdPath = "/proc/self/fd/" + fd;
                            Log.i(TAG, "Detached ISO file descriptor: " + fd + " -> " + procFdPath);
                            runOnUiThread(() -> {
                                if (layoutLoading != null) layoutLoading.setVisibility(View.GONE);
                                Toast.makeText(this, "ISO vinculada instantaneamente via descritor de arquivo!", Toast.LENGTH_SHORT).show();
                                launchGame(procFdPath);
                            });
                            return;
                        }
                    }
                } catch (Exception e) {
                    Log.w(TAG, "Unable to detach file descriptor for URI: " + e.getMessage() + "; falling back to cache copy");
                }

                // Fallback: Copy to cache dir
                File cacheTarget = new File(getCacheDir(), fileName);
                Log.i(TAG, "Copying ISO content URI to cache: " + cacheTarget.getAbsolutePath());

                runOnUiThread(() -> {
                    if (tvLoadingText != null) {
                        tvLoadingText.setText("Copiando " + cacheTarget.getName() + " para o cache do jogo...");
                    }
                });

                try (InputStream in = getContentResolver().openInputStream(uri);
                     FileOutputStream out = new FileOutputStream(cacheTarget)) {
                    if (in == null) {
                        runOnUiThread(() -> {
                            if (layoutLoading != null) layoutLoading.setVisibility(View.GONE);
                            Toast.makeText(this, "Erro ao abrir o arquivo ISO selecionado.", Toast.LENGTH_LONG).show();
                        });
                        return;
                    }
                    byte[] buffer = new byte[1024 * 1024]; // 1MB buffer
                    int read;
                    long totalBytes = 0;
                    long lastUpdate = System.currentTimeMillis();
                    while ((read = in.read(buffer)) != -1) {
                        out.write(buffer, 0, read);
                        totalBytes += read;
                        long now = System.currentTimeMillis();
                        if (now - lastUpdate > 500) {
                            lastUpdate = now;
                            final long mb = totalBytes / (1024 * 1024);
                            runOnUiThread(() -> {
                                if (tvLoadingText != null) {
                                    tvLoadingText.setText("Copiando ISO (" + mb + " MB transferidos)...");
                                }
                            });
                        }
                    }
                    out.flush();
                }

                String resolvedPath = cacheTarget.getAbsolutePath();
                runOnUiThread(() -> {
                    if (layoutLoading != null) layoutLoading.setVisibility(View.GONE);
                    Toast.makeText(this, "ISO carregada com sucesso! Extraindo dados...", Toast.LENGTH_SHORT).show();
                    launchGame(resolvedPath);
                });

            } catch (Exception e) {
                Log.e(TAG, "Failed to resolve ISO file: " + e.getMessage(), e);
                runOnUiThread(() -> {
                    if (layoutLoading != null) layoutLoading.setVisibility(View.GONE);
                    Toast.makeText(this, "Falha ao processar arquivo ISO: " + e.getMessage(), Toast.LENGTH_LONG).show();
                });
            }
        }).start();
    }

    private void launchGame(final String isoPath) {
        if (btnStartGame != null) {
            btnStartGame.setEnabled(false);
        }
        if (btnSettings != null) {
            btnSettings.setEnabled(false);
        }

        boolean turnipRequested = GameConfigManager.isTurnipEnabled(this);
        GameConfigManager.markTurnipLaunchInProgress(this, turnipRequested);

        if (fadeOverlay != null) {
            fadeOverlay.setVisibility(View.VISIBLE);
            fadeOverlay.setAlpha(0f);
            fadeOverlay.animate()
                .alpha(1f)
                .setDuration(400)
                .withEndAction(() -> {
                    Intent intent = new Intent(TitleActivity.this, MainActivity.class);
                    if (isoPath != null && !isoPath.isEmpty()) {
                        intent.putExtra(GameConfigManager.EXTRA_ISO_PATH, isoPath);
                    }
                    startActivity(intent);
                })
                .start();
        } else {
            Intent intent = new Intent(TitleActivity.this, MainActivity.class);
            if (isoPath != null && !isoPath.isEmpty()) {
                intent.putExtra(GameConfigManager.EXTRA_ISO_PATH, isoPath);
            }
            startActivity(intent);
        }
    }

    private String getFileName(Uri uri) {
        String result = null;
        if (uri.getScheme() != null && uri.getScheme().equals("content")) {
            try (Cursor cursor = getContentResolver().query(uri, null, null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) {
                    int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (index >= 0) {
                        result = cursor.getString(index);
                    }
                }
            } catch (Exception ignored) {}
        }
        if (result == null) {
            result = uri.getPath();
            if (result != null) {
                int cut = result.lastIndexOf('/');
                if (cut != -1) {
                    result = result.substring(cut + 1);
                }
            }
        }
        return result;
    }

    private void checkStoragePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                new AlertDialog.Builder(this)
                    .setTitle("Acesso aos Arquivos do Jogo")
                    .setMessage("O Dante's Inferno precisa de acesso aos arquivos para ler o arquivo ISO e extrair o conteúdo do jogo.")
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
}
