package com.dantesinferno.game;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;
import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import androidx.appcompat.widget.SwitchCompat;

public class SettingsActivity extends AppCompatActivity {

    private static final int REQUEST_CODE_DRIVER_ZIP = 2001;

    private SwitchCompat switchUseTurnip;
    private SwitchCompat switchTurboMode;
    private TextView tvDriverStatus;
    private Button btnInstallDriverZip;
    private Button btnResetSystemDriver;

    private Spinner spinnerResScale;
    private Spinner spinnerFpsLimit;
    private SwitchCompat switchVsync;
    private TextView tvShaderCacheSize;
    private Button btnClearShaderCache;
    private Button btnApplyQuickSettings;

    private SwitchCompat switchVirtualController;
    private Spinner spinnerControllerOpacity;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_settings);

        initViews();
        setupDriverSection();
        setupGraphicsSection();
        setupControllerSection();
    }

    private void initViews() {
        View btnBack = findViewById(R.id.btn_back);
        if (btnBack != null) {
            btnBack.setOnClickListener(v -> finish());
        }

        switchUseTurnip = findViewById(R.id.switch_use_turnip);
        switchTurboMode = findViewById(R.id.switch_turbo_mode);
        tvDriverStatus = findViewById(R.id.tv_settings_driver_name);
        btnInstallDriverZip = findViewById(R.id.btn_install_driver_zip);
        btnResetSystemDriver = findViewById(R.id.btn_reset_system_driver);

        spinnerResScale = findViewById(R.id.spinner_resolution_scale);
        spinnerFpsLimit = findViewById(R.id.spinner_fps_limit);
        switchVsync = findViewById(R.id.switch_vsync);
        tvShaderCacheSize = findViewById(R.id.tv_shader_cache_size);
        btnClearShaderCache = findViewById(R.id.btn_clear_shader_cache);
        btnApplyQuickSettings = findViewById(R.id.btn_apply_quick_settings);

        switchVirtualController = findViewById(R.id.switch_virtual_controller);
        spinnerControllerOpacity = findViewById(R.id.spinner_controller_opacity);
    }

    private void setupDriverSection() {
        boolean useTurnip = GameConfigManager.isTurnipEnabled(this);
        boolean turbo = GameConfigManager.isTurboEnabled(this);

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

        if (btnInstallDriverZip != null) {
            btnInstallDriverZip.setOnClickListener(v -> launchDriverPicker());
        }

        if (btnResetSystemDriver != null) {
            btnResetSystemDriver.setOnClickListener(v -> {
                GameConfigManager.setTurnipEnabled(this, false);
                GameConfigManager.markTurnipLaunchInProgress(this, false);
                if (switchUseTurnip != null) switchUseTurnip.setChecked(false);
                updateDriverStatusText();
                Toast.makeText(this, "Driver do Sistema (Qualcomm OEM) definido como padrão.", Toast.LENGTH_SHORT).show();
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

    private void setupGraphicsSection() {
        SharedPreferences prefs = getSharedPreferences(GameConfigManager.PREF_NAME, MODE_PRIVATE);

        // Resolution Scale Spinner
        if (spinnerResScale != null) {
            String[] scales = new String[] { "720p (Nativo X360 - 1x)", "1080p (Alta Qualidade - 1.5x)", "1440p (2x SSAA)", "540p (Modo Desempenho)" };
            ArrayAdapter<String> scaleAdapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, scales);
            spinnerResScale.setAdapter(scaleAdapter);
            int savedScale = prefs.getInt("resolution_scale_idx", 0);
            spinnerResScale.setSelection(savedScale);

            spinnerResScale.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
                @Override
                public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                    prefs.edit().putInt("resolution_scale_idx", position).apply();
                }

                @Override
                public void onNothingSelected(AdapterView<?> parent) {}
            });
        }

        // FPS Limit Spinner
        if (spinnerFpsLimit != null) {
            String[] fpsOptions = new String[] { "60 FPS (Padrão)", "30 FPS (Modo Bateria)", "Ilimitado" };
            ArrayAdapter<String> fpsAdapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, fpsOptions);
            spinnerFpsLimit.setAdapter(fpsAdapter);
            int savedFps = prefs.getInt("fps_limit_idx", 0);
            spinnerFpsLimit.setSelection(savedFps);

            spinnerFpsLimit.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
                @Override
                public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                    prefs.edit().putInt("fps_limit_idx", position).apply();
                }

                @Override
                public void onNothingSelected(AdapterView<?> parent) {}
            });
        }

        // VSync Switch
        if (switchVsync != null) {
            boolean vsync = prefs.getBoolean("vsync_enabled", true);
            switchVsync.setChecked(vsync);
            switchVsync.setOnCheckedChangeListener((bv, isChecked) -> {
                prefs.edit().putBoolean("vsync_enabled", isChecked).apply();
            });
        }

        // Clear Cache
        if (btnClearShaderCache != null) {
            btnClearShaderCache.setOnClickListener(v -> {
                GameConfigManager.clearCache(this);
                updateShaderCacheDisplay();
                Toast.makeText(this, "Cache de shaders limpo!", Toast.LENGTH_SHORT).show();
            });
        }

        if (btnApplyQuickSettings != null) {
            btnApplyQuickSettings.setOnClickListener(v -> {
                Toast.makeText(this, "Configurações gráficas salvas com sucesso!", Toast.LENGTH_SHORT).show();
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

    private void setupControllerSection() {
        SharedPreferences prefs = getSharedPreferences(GameConfigManager.PREF_NAME, MODE_PRIVATE);

        if (switchVirtualController != null) {
            boolean showVc = prefs.getBoolean("show_virtual_controller", true);
            switchVirtualController.setChecked(showVc);
            switchVirtualController.setOnCheckedChangeListener((buttonView, isChecked) -> {
                prefs.edit().putBoolean("show_virtual_controller", isChecked).apply();
                Toast.makeText(this, isChecked ? "Controles virtuais ativados." : "Controles virtuais desativados.", Toast.LENGTH_SHORT).show();
            });
        }

        if (spinnerControllerOpacity != null) {
            String[] opacities = new String[] { "70% (Padrão)", "100% (Totalmente Visível)", "50% (Sutil)", "25% (Muito Transparente)" };
            ArrayAdapter<String> opAdapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, opacities);
            spinnerControllerOpacity.setAdapter(opAdapter);

            int currentOp = prefs.getInt("controller_opacity", 70);
            if (currentOp == 100) spinnerControllerOpacity.setSelection(1);
            else if (currentOp == 50) spinnerControllerOpacity.setSelection(2);
            else if (currentOp == 25) spinnerControllerOpacity.setSelection(3);
            else spinnerControllerOpacity.setSelection(0);

            spinnerControllerOpacity.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
                @Override
                public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                    int op = (position == 1) ? 100 : (position == 2 ? 50 : (position == 3 ? 25 : 70));
                    prefs.edit().putInt("controller_opacity", op).apply();
                }

                @Override
                public void onNothingSelected(AdapterView<?> parent) {}
            });
        }
    }
}
