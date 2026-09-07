package com.dantesinferno.game;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.SharedPreferences;
import android.util.AttributeSet;
import android.util.Log;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.widget.RelativeLayout;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLControllerManager;

/**
 * Controller overlay container inflating layout_virtual_controller.xml.
 * Manages button touch events, analog sticks, D-pad, and SDL3 event dispatching.
 * Imported from WINDROID-EMU/Silent-Hill-Downpour-port-android.
 */
public class VirtualControllerLayout extends RelativeLayout {

    private static final String TAG = "VirtualControllerLayout";
    public static final int VIRTUAL_DEVICE_ID = 9999;
    public static final String PREF_NAME = "dantes_settings";

    private static boolean joystickRegistered = false;

    private VirtualJoystickView stickLeft;
    private VirtualJoystickView stickRight;
    private VirtualDpadView dpad;

    private View btnA, btnB, btnX, btnY;
    private View btnLb, btnLt, btnRb, btnRt;
    private View btnStart, btnSelect, btnL3, btnR3;

    // Real-Time FPS HUD
    private View hudFpsContainer;
    private android.widget.TextView tvHudFps;
    private android.widget.TextView tvHudFrametime;
    private android.view.Choreographer.FrameCallback mFpsCallback;
    private long mLastFrameTimeNanos = 0;
    private int mFrameCounter = 0;
    private long mLastFpsUpdateNanos = 0;

    public VirtualControllerLayout(Context context) {
        super(context);
        init();
    }

    public VirtualControllerLayout(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public VirtualControllerLayout(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    private void init() {
        setFocusable(false);
        setFocusableInTouchMode(false);

        // Inflate the XML layout directly
        LayoutInflater.from(getContext()).inflate(R.layout.layout_virtual_controller, this, true);

        // Apply saved opacity to controls only (so HUD remains crisp and legible)
        SharedPreferences prefs = getContext().getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);
        int opacity = prefs.getInt("controller_opacity", 70);
        float alpha = Math.max(0.15f, Math.min(1.0f, opacity / 100f));
        View controlsContainer = findViewById(R.id.layout_controls_container);
        if (controlsContainer != null) {
            controlsContainer.setAlpha(alpha);
        } else {
            setAlpha(alpha);
        }

        registerVirtualJoystick();
        bindViews();
        setupListeners();
        setupFpsCounter();
    }

    public void registerVirtualJoystick() {
        if (joystickRegistered) return;
        try {
            SDLControllerManager.nativeAddJoystick(
                VIRTUAL_DEVICE_ID,
                "Virtual Xbox Controller",
                "Virtual Gamepad",
                0x045E, // Microsoft
                0x028E, // Xbox 360 Controller
                0xFFFF, // Button mask
                6,      // 6 axes (LX, LY, RX, RY, LT, RT)
                0x3F,   // Axis mask
                1,      // 1 Hat (D-Pad)
                false,
                false
            );
            joystickRegistered = true;
            Log.i(TAG, "Virtual joystick registered in SDL3 with deviceId " + VIRTUAL_DEVICE_ID);
        } catch (Throwable t) {
            Log.w(TAG, "Could not call nativeAddJoystick: " + t.getMessage());
        }
    }

    private void bindViews() {
        stickLeft = findViewById(R.id.stick_left);
        stickRight = findViewById(R.id.stick_right);
        dpad = findViewById(R.id.dpad);

        btnA = findViewById(R.id.btn_a);
        btnB = findViewById(R.id.btn_b);
        btnX = findViewById(R.id.btn_x);
        btnY = findViewById(R.id.btn_y);

        btnLt = findViewById(R.id.btn_lt);
        btnLb = findViewById(R.id.btn_lb);
        btnRt = findViewById(R.id.btn_rt);
        btnRb = findViewById(R.id.btn_rb);

        btnSelect = findViewById(R.id.btn_select);
        btnStart = findViewById(R.id.btn_start);
        btnL3 = findViewById(R.id.btn_l3);
        btnR3 = findViewById(R.id.btn_r3);

        hudFpsContainer = findViewById(R.id.hud_fps_container);
        tvHudFps = findViewById(R.id.tv_hud_fps);
        tvHudFrametime = findViewById(R.id.tv_hud_frametime);
    }

    private void setupFpsCounter() {
        boolean showFps = GameConfigManager.isShowFpsEnabled(getContext());
        if (hudFpsContainer == null) return;

        if (!showFps) {
            hudFpsContainer.setVisibility(View.GONE);
            return;
        }

        // Keep HUD visible at 100% opacity regardless of controller opacity
        hudFpsContainer.setVisibility(View.VISIBLE);
        hudFpsContainer.setAlpha(1.0f);

        mFpsCallback = new android.view.Choreographer.FrameCallback() {
            @Override
            public void doFrame(long frameTimeNanos) {
                if (mLastFrameTimeNanos > 0) {
                    mFrameCounter++;
                    long deltaNanos = frameTimeNanos - mLastFpsUpdateNanos;
                    if (deltaNanos >= 500_000_000L) { // Update twice a second (every 500ms) for smooth reading
                        double fps = (mFrameCounter * 1_000_000_000.0) / deltaNanos;
                        double frameTimeMs = 1000.0 / Math.max(1.0, fps);

                        if (tvHudFps != null) {
                            tvHudFps.setText(String.format(java.util.Locale.US, "%.1f FPS", fps));
                            if (fps >= 55.0) {
                                tvHudFps.setTextColor(0xFF3FB950); // Vibrant Green
                            } else if (fps >= 30.0) {
                                tvHudFps.setTextColor(0xFFE3B341); // Yellow / Gold
                            } else {
                                tvHudFps.setTextColor(0xFFF85149); // Red
                            }
                        }
                        if (tvHudFrametime != null) {
                            tvHudFrametime.setText(String.format(java.util.Locale.US, "%.1f ms", frameTimeMs));
                        }

                        mFrameCounter = 0;
                        mLastFpsUpdateNanos = frameTimeNanos;
                    }
                } else {
                    mLastFpsUpdateNanos = frameTimeNanos;
                }
                mLastFrameTimeNanos = frameTimeNanos;

                if (isAttachedToWindow()) {
                    android.view.Choreographer.getInstance().postFrameCallback(this);
                }
            }
        };

        android.view.Choreographer.getInstance().postFrameCallback(mFpsCallback);
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        if (mFpsCallback != null && GameConfigManager.isShowFpsEnabled(getContext())) {
            android.view.Choreographer.getInstance().postFrameCallback(mFpsCallback);
        }
    }

    @Override
    protected void onDetachedFromWindow() {
        super.onDetachedFromWindow();
        if (mFpsCallback != null) {
            android.view.Choreographer.getInstance().removeFrameCallback(mFpsCallback);
        }
    }

    private void setupListeners() {
        // Left Analog Stick
        if (stickLeft != null) {
            stickLeft.setOnJoystickMoveListener((x, y) -> {
                dispatchAnalog(0, x); // Left X
                dispatchAnalog(1, y); // Left Y
            });
        }

        // Right Analog Stick
        if (stickRight != null) {
            stickRight.setOnJoystickMoveListener((x, y) -> {
                dispatchAnalog(2, x); // Right X
                dispatchAnalog(3, y); // Right Y
            });
        }

        // D-Pad Cross
        if (dpad != null) {
            dpad.setOnDpadListener((up, down, left, right, hatX, hatY) -> {
                try {
                    SDLControllerManager.onNativeHat(VIRTUAL_DEVICE_ID, 0, hatX, hatY);
                } catch (Throwable ignored) {}

                dispatchKey(KeyEvent.KEYCODE_DPAD_UP, up);
                dispatchKey(KeyEvent.KEYCODE_DPAD_DOWN, down);
                dispatchKey(KeyEvent.KEYCODE_DPAD_LEFT, left);
                dispatchKey(KeyEvent.KEYCODE_DPAD_RIGHT, right);
            });
        }

        // Action buttons
        bindButtonTouch(btnA, KeyEvent.KEYCODE_BUTTON_A);
        bindButtonTouch(btnB, KeyEvent.KEYCODE_BUTTON_B);
        bindButtonTouch(btnX, KeyEvent.KEYCODE_BUTTON_X);
        bindButtonTouch(btnY, KeyEvent.KEYCODE_BUTTON_Y);

        // Bumpers & Triggers
        bindButtonTouch(btnLb, KeyEvent.KEYCODE_BUTTON_L1);
        bindButtonTouch(btnRb, KeyEvent.KEYCODE_BUTTON_R1);

        bindTriggerTouch(btnLt, KeyEvent.KEYCODE_BUTTON_L2, 4);
        bindTriggerTouch(btnRt, KeyEvent.KEYCODE_BUTTON_R2, 5);

        // Menu & Stick Click buttons
        bindButtonTouch(btnSelect, KeyEvent.KEYCODE_BUTTON_SELECT);
        bindButtonTouch(btnStart, KeyEvent.KEYCODE_BUTTON_START);
        bindButtonTouch(btnL3, KeyEvent.KEYCODE_BUTTON_THUMBL);
        bindButtonTouch(btnR3, KeyEvent.KEYCODE_BUTTON_THUMBR);
    }

    @SuppressLint("ClickableViewAccessibility")
    private void bindButtonTouch(View view, int keyCode) {
        if (view == null) return;
        view.setOnTouchListener((v, event) -> {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    v.setPressed(true);
                    dispatchButton(keyCode, true);
                    return true;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    v.setPressed(false);
                    dispatchButton(keyCode, false);
                    return true;
            }
            return false;
        });
    }

    @SuppressLint("ClickableViewAccessibility")
    private void bindTriggerTouch(View view, int keyCode, int axis) {
        if (view == null) return;
        view.setOnTouchListener((v, event) -> {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    v.setPressed(true);
                    dispatchButton(keyCode, true);
                    dispatchAnalog(axis, 1.0f);
                    return true;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    v.setPressed(false);
                    dispatchButton(keyCode, false);
                    dispatchAnalog(axis, 0.0f);
                    return true;
            }
            return false;
        });
    }

    private void dispatchButton(int keyCode, boolean pressed) {
        try {
            if (pressed) {
                SDLControllerManager.onNativePadDown(VIRTUAL_DEVICE_ID, keyCode);
            } else {
                SDLControllerManager.onNativePadUp(VIRTUAL_DEVICE_ID, keyCode);
            }
        } catch (Throwable ignored) {}

        try {
            if (pressed) {
                SDLActivity.onNativeKeyDown(keyCode);
            } else {
                SDLActivity.onNativeKeyUp(keyCode);
            }
        } catch (Throwable ignored) {}
    }

    private void dispatchKey(int keyCode, boolean pressed) {
        try {
            if (pressed) {
                SDLControllerManager.onNativePadDown(VIRTUAL_DEVICE_ID, keyCode);
                SDLActivity.onNativeKeyDown(keyCode);
            } else {
                SDLControllerManager.onNativePadUp(VIRTUAL_DEVICE_ID, keyCode);
                SDLActivity.onNativeKeyUp(keyCode);
            }
        } catch (Throwable ignored) {}
    }

    private void dispatchAnalog(int axis, float value) {
        try {
            SDLControllerManager.onNativeJoy(VIRTUAL_DEVICE_ID, axis, value);
        } catch (Throwable ignored) {}
    }
}
