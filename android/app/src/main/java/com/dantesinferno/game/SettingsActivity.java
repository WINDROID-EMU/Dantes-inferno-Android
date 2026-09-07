package com.dantesinferno.game;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.SwitchCompat;

public class SettingsActivity extends AppCompatActivity {

    private static final int REQUEST_CODE_DRIVER_ZIP = 2001;

    // Driver & Debug Controls
    private SwitchCompat switchUseTurnip;
    private SwitchCompat switchTurboMode;
    private SwitchCompat switchDisableDebug;
    private SwitchCompat switchShowFps;
    private TextView tvDriverStatus;
    private Button btnInstallDriverZip;
    private Button btnResetSystemDriver;

    // Virtual Controls
    private SwitchCompat switchVirtualController;
    private Spinner spinnerControllerOpacity;

    // Graphics Controls
    private Spinner spinnerResScale;
    private SwitchCompat switchVsync;
    private Spinner spinnerPresentEffect;
    private Spinner spinnerVulkanPresentMode;
    private SwitchCompat switchAsyncShaders;
    private Spinner spinnerShaderThreads;

    // Shader Cache
    private TextView tvShaderCacheSize;
    private Button btnClearShaderCache;
    private Button btnApplyQuickSettings;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_settings);

        initViews();
        setupDriverSection();
        setupVirtualControllerSection();
        setupGraphicsSection();
        setupCacheSection();
    }

    private void initViews() {
        View btnBack = findViewById(R.id.btn_back);
        if (btnBack != null) {
            btnBack.setOnClickListener(v -> {
                GameConfigManager.saveTomlConfig(this);
                finish();
            });
        }

        switchUseTurnip = findViewById(R.id.switch_use_turnip);
        switchTurboMode = findViewById(R.id.switch_turbo_mode);
        switchDisableDebug = findViewById(R.id.switch_disable_debug);
        switchShowFps = findViewById(R.id.switch_show_fps);
        tvDriverStatus = findViewById(R.id.tv_settings_driver_name);
        btnInstallDriverZip = findViewById(R.id.btn_install_driver_zip);
        btnResetSystemDriver = findViewById(R.id.btn_reset_system_driver);

        switchVirtualController = findViewById(R.id.switch_virtual_controller);
        spinnerControllerOpacity = findViewById(R.id.spinner_controller_opacity);

        spinnerResScale = findViewById(R.id.spinner_resolution_scale);
        switchVsync = findViewById(R.id.switch_vsync);
        spinnerPresentEffect = findViewById(R.id.spinner_present_effect);
        spinnerVulkanPresentMode = findViewById(R.id.spinner_vulkan_present_mode);
        switchAsyncShaders = findViewById(R.id.switch_async_shaders);
        spinnerShaderThreads = findViewById(R.id.spinner_shader_threads);

        tvShaderCacheSize = findViewById(R.id.tv_shader_cache_size);
        btnClearShaderCache = findViewById(R.id.btn_clear_shader_cache);
        btnApplyQuickSettings = findViewById(R.id.btn_apply_quick_settings);
    }

    private void setupDriverSection() {
        boolean useTurnip = GameConfigManager.isTurnipEnabled(this);
        boolean turbo = GameConfigManager.isTurboEnabled(this);
        boolean disableDebug = GameConfigManager.isDisableDebug(this);

        if (switchUseTurnip != null) {
            switchUseTurnip.setChecked(useTurnip);
            switchUseTurnip.setOnCheckedChangeListener((bv, isChecked) -> {
                GameConfigManager.setTurnipEnabled(this, isChecked);
                if (isChecked) {
                    GameConfigManager.markTurnipLaunchInProgress(this, false);
                }
                updateDriverStatusText();
                String msg = isChecked ? "Driver Turnip ativado!" : "Driver do Sistema Qualcomm ativado (Estável)!";
                Toast.makeText(this, msg, Toast.LENGTH_SHORT).show();
            });
        }

        if (switchTurboMode != null) {
            switchTurboMode.setChecked(turbo);
            switchTurboMode.setOnCheckedChangeListener((bv, isChecked) -> {
                GameConfigManager.setTurboEnabled(this, isChecked);
            });
        }

        if (switchDisableDebug != null) {
            switchDisableDebug.setChecked(disableDebug);
            switchDisableDebug.setOnCheckedChangeListener((bv, isChecked) -> {
                GameConfigManager.setDisableDebug(this, isChecked);
            });
        }

        if (switchShowFps != null) {
            switchShowFps.setChecked(GameConfigManager.isShowFpsEnabled(this));
            switchShowFps.setOnCheckedChangeListener((bv, isChecked) -> {
                GameConfigManager.setShowFpsEnabled(this, isChecked);
            });
        }

        if (btnInstallDriverZip != null) {
            btnInstallDriverZip.setOnClickListener(v -> launchDriverPicker());
        }

        if (btnResetSystemDriver != null) {
            btnResetSystemDriver.setOnClickListener(v -> {
                GameConfigManager.setTurnipEnabled(this, false);
                GameConfigManager.markTurnipLaunchInProgress(this, false);
                if (switchUseTurnip != null) switchUseTurnip.setChecked(false);
                updateDriverStatusText();
                Toast.makeText(this, "Driver do Sistema (Qualcomm OEM) restaurado.", Toast.LENGTH_SHORT).show();
            });
        }

        updateDriverStatusText();
    }

    private void updateDriverStatusText() {
        if (tvDriverStatus == null) return;
        tvDriverStatus.setText(GameConfigManager.getActiveDriverDescription(this));
        boolean active = GameConfigManager.isTurnipEnabled(this) && GameConfigManager.hasCustomDriverInstalled(this);
        tvDriverStatus.setTextColor(active ? 0xFF7EE787 : 0xFF8B949E);
    }

    private void launchDriverPicker() {
        Toast.makeText(this, "Selecione o arquivo ZIP do driver Turnip (AdrenoTools)", Toast.LENGTH_LONG).show();
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQUEST_CODE_DRIVER_ZIP);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_CODE_DRIVER_ZIP && resultCode == Activity.RESULT_OK && data != null && data.getData() != null) {
            Uri uri = data.getData();
            Toast.makeText(this, "Extraindo pacote de driver Turnip...", Toast.LENGTH_SHORT).show();
            new Thread(() -> {
                try {
                    String driverName = GameConfigManager.extractDriverZip(this, uri);
                    runOnUiThread(() -> {
                        if (driverName != null) {
                            if (switchUseTurnip != null) switchUseTurnip.setChecked(true);
                            updateDriverStatusText();
                            Toast.makeText(this, "Driver Turnip '" + driverName + "' instalado com sucesso!", Toast.LENGTH_LONG).show();
                        } else {
                            Toast.makeText(this, "Nenhuma biblioteca Vulkan .so encontrada no arquivo ZIP.", Toast.LENGTH_LONG).show();
                        }
                    });
                } catch (Exception e) {
                    runOnUiThread(() -> {
                        Toast.makeText(this, "Erro ao extrair driver: " + e.getMessage(), Toast.LENGTH_LONG).show();
                    });
                }
            }).start();
        }
    }

    private void setupVirtualControllerSection() {
        if (switchVirtualController != null) {
            boolean showVc = GameConfigManager.isShowVirtualController(this);
            switchVirtualController.setChecked(showVc);
            switchVirtualController.setOnCheckedChangeListener((bv, isChecked) -> {
                GameConfigManager.setShowVirtualController(this, isChecked);
            });
        }

        if (spinnerControllerOpacity != null) {
            String[] opacities = new String[] { "70% (Padrão)", "100% (Máxima Visibilidade)", "50% (Sutil)", "25% (Muito Transparente)" };
            ArrayAdapter<String> opAdapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, opacities);
            spinnerControllerOpacity.setAdapter(opAdapter);

            int currentOp = GameConfigManager.getControllerOpacity(this);
            if (currentOp == 100) spinnerControllerOpacity.setSelection(1);
            else if (currentOp == 50) spinnerControllerOpacity.setSelection(2);
            else if (currentOp == 25) spinnerControllerOpacity.setSelection(3);
            else spinnerControllerOpacity.setSelection(0);

            spinnerControllerOpacity.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
                @Override
                public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                    int op = (position == 1) ? 100 : (position == 2 ? 50 : (position == 3 ? 25 : 70));
                    GameConfigManager.setControllerOpacity(SettingsActivity.this, op);
                }

                @Override
                public void onNothingSelected(AdapterView<?> parent) {}
            });
        }
    }

    private void setupGraphicsSection() {
        // Resolution Scale Spinner
        if (spinnerResScale != null) {
            String[] scales = new String[] {
                "720p (Nativo - 1x)",
                "1080p (Alta Qualidade - 2x)",
                "1440p (SSAA Máximo - 2x)",
                "540p (Desempenho)"
            };
            ArrayAdapter<String> scaleAdapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, scales);
            spinnerResScale.setAdapter(scaleAdapter);
            int savedScale = GameConfigManager.getResolutionScaleIdx(this);
            spinnerResScale.setSelection(savedScale);

            spinnerResScale.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
                @Override
                public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                    GameConfigManager.setResolutionScaleIdx(SettingsActivity.this, position);
                }

                @Override
                public void onNothingSelected(AdapterView<?> parent) {}
            });
        }

        // VSync Switch
        if (switchVsync != null) {
            boolean vsync = GameConfigManager.isVsyncEnabled(this);
            switchVsync.setChecked(vsync);
            switchVsync.setOnCheckedChangeListener((bv, isChecked) -> {
                GameConfigManager.setVsyncEnabled(this, isChecked);
            });
        }

        // Present Effect / Upscaler Spinner
        if (spinnerPresentEffect != null) {
            String[] effects = new String[] {
                "Nenhum (Bilinear)",
                "FXAA (Anti-Aliasing)",
                "CAS (AMD Sharpening)",
                "FSR (AMD Super Res)"
            };
            ArrayAdapter<String> effectAdapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, effects);
            spinnerPresentEffect.setAdapter(effectAdapter);
            int savedEffect = GameConfigManager.getPresentEffectIdx(this);
            spinnerPresentEffect.setSelection(savedEffect);

            spinnerPresentEffect.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
                @Override
                public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                    GameConfigManager.setPresentEffectIdx(SettingsActivity.this, position);
                }

                @Override
                public void onNothingSelected(AdapterView<?> parent) {}
            });
        }

        // Vulkan Present Mode Spinner
        if (spinnerVulkanPresentMode != null) {
            String[] modes = new String[] {
                "FIFO (VSync Padrão)",
                "Mailbox (Baixa Latência)",
                "Immediate (Sem VSync)"
            };
            ArrayAdapter<String> modeAdapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, modes);
            spinnerVulkanPresentMode.setAdapter(modeAdapter);
            int savedMode = GameConfigManager.getVulkanPresentModeIdx(this);
            spinnerVulkanPresentMode.setSelection(savedMode);

            spinnerVulkanPresentMode.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
                @Override
                public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                    GameConfigManager.setVulkanPresentModeIdx(SettingsActivity.this, position);
                }

                @Override
                public void onNothingSelected(AdapterView<?> parent) {}
            });
        }

        // Async Shader Compilation Switch
        if (switchAsyncShaders != null) {
            boolean asyncShaders = GameConfigManager.isAsyncShadersEnabled(this);
            switchAsyncShaders.setChecked(asyncShaders);
            switchAsyncShaders.setOnCheckedChangeListener((bv, isChecked) -> {
                GameConfigManager.setAsyncShadersEnabled(this, isChecked);
            });
        }

        // Pipeline Threads Spinner
        if (spinnerShaderThreads != null) {
            String[] threads = new String[] {
                "Automático (Recomendado)",
                "2 Threads",
                "4 Threads",
                "1 Thread (Econômico)"
            };
            ArrayAdapter<String> threadAdapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, threads);
            spinnerShaderThreads.setAdapter(threadAdapter);
            int savedThreads = GameConfigManager.getPipelineThreadsIdx(this);
            spinnerShaderThreads.setSelection(savedThreads);


            spinnerShaderThreads.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
                @Override
                public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                    GameConfigManager.setPipelineThreadsIdx(SettingsActivity.this, position);
                }

                @Override
                public void onNothingSelected(AdapterView<?> parent) {}
            });
        }
    }

    private void setupCacheSection() {
        if (btnClearShaderCache != null) {
            btnClearShaderCache.setOnClickListener(v -> {
                GameConfigManager.clearCache(this);
                updateShaderCacheDisplay();
                Toast.makeText(this, "Cache de shaders limpo!", Toast.LENGTH_SHORT).show();
            });
        }

        if (btnApplyQuickSettings != null) {
            btnApplyQuickSettings.setOnClickListener(v -> {
                GameConfigManager.saveTomlConfig(this);
                Toast.makeText(this, "Configurações salvas e aplicadas com sucesso!", Toast.LENGTH_SHORT).show();
                finish();
            });
        }

        updateShaderCacheDisplay();
    }

    private void updateShaderCacheDisplay() {
        if (tvShaderCacheSize == null) return;
        long bytes = GameConfigManager.getCacheSizeBytes(this);
        double mb = bytes / (1024.0 * 1024.0);
        tvShaderCacheSize.setText(String.format(java.util.Locale.US, "%.2f MB", mb));
    }
}
