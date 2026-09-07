package com.dantesinferno.game;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;

import androidx.annotation.NonNull;

/**
 * Modular virtual analog stick component that can be placed directly in XML layouts.
 * Imported from WINDROID-EMU/Silent-Hill-Downpour-port-android.
 */
public class VirtualJoystickView extends View {

    public interface OnJoystickMoveListener {
        void onValueChanged(float x, float y);
    }

    private Paint basePaint;
    private Paint strokePaint;
    private Paint knobPaint;
    private Paint knobStrokePaint;

    private float centerX = 0f;
    private float centerY = 0f;
    private float baseRadius = 0f;
    private float knobRadius = 0f;

    private float fingerX = 0f;
    private float fingerY = 0f;
    private boolean isPressed = false;
    private int activePointerId = -1;

    private OnJoystickMoveListener listener;

    public VirtualJoystickView(Context context) {
        super(context);
        init();
    }

    public VirtualJoystickView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public VirtualJoystickView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    private void init() {
        setFocusable(false);

        float density = getResources().getDisplayMetrics().density;

        basePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        basePaint.setStyle(Paint.Style.FILL);
        basePaint.setColor(0x59000000);

        strokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        strokePaint.setStyle(Paint.Style.STROKE);
        strokePaint.setColor(0xB3FFFFFF);
        strokePaint.setStrokeWidth(2.2f * density);

        knobPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        knobPaint.setStyle(Paint.Style.FILL);
        knobPaint.setColor(Color.WHITE);

        knobStrokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        knobStrokePaint.setStyle(Paint.Style.STROKE);
        knobStrokePaint.setColor(0xCCFFFFFF);
        knobStrokePaint.setStrokeWidth(2.0f * density);
    }

    public void setOnJoystickMoveListener(OnJoystickMoveListener l) {
        this.listener = l;
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        centerX = w / 2f;
        centerY = h / 2f;
        baseRadius = Math.min(w, h) / 2f - strokePaint.getStrokeWidth();
        knobRadius = baseRadius * 0.38f;
    }

    @Override
    protected void onDraw(@NonNull Canvas canvas) {
        super.onDraw(canvas);

        // Draw Base Circle
        canvas.drawCircle(centerX, centerY, baseRadius, basePaint);
        canvas.drawCircle(centerX, centerY, baseRadius, strokePaint);

        // Subtle guide ring
        strokePaint.setAlpha(60);
        canvas.drawCircle(centerX, centerY, baseRadius * 0.40f, strokePaint);
        strokePaint.setAlpha(180);

        // Draw Knob
        float kx = centerX + fingerX;
        float ky = centerY + fingerY;

        if (isPressed) {
            knobPaint.setColor(0xFF58A6FF);
        } else {
            knobPaint.setColor(0xCCFFFFFF);
        }
        canvas.drawCircle(kx, ky, knobRadius, knobPaint);
        canvas.drawCircle(kx, ky, knobRadius, knobStrokePaint);
    }

    @SuppressLint("ClickableViewAccessibility")
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int actionMasked = event.getActionMasked();
        int actionIndex = event.getActionIndex();

        switch (actionMasked) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                if (!isPressed) {
                    activePointerId = event.getPointerId(actionIndex);
                    isPressed = true;
                    updatePosition(event.getX(actionIndex), event.getY(actionIndex));
                    return true;
                }
                break;
            }

            case MotionEvent.ACTION_MOVE: {
                if (isPressed) {
                    int pointerIndex = event.findPointerIndex(activePointerId);
                    if (pointerIndex != -1) {
                        updatePosition(event.getX(pointerIndex), event.getY(pointerIndex));
                        return true;
                    }
                }
                break;
            }

            case MotionEvent.ACTION_POINTER_UP: {
                int pointerId = event.getPointerId(actionIndex);
                if (pointerId == activePointerId) {
                    resetJoystick();
                    return true;
                }
                break;
            }

            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL: {
                resetJoystick();
                return true;
            }
        }

        return super.onTouchEvent(event);
    }

    private void updatePosition(float touchX, float touchY) {
        float dx = touchX - centerX;
        float dy = touchY - centerY;
        float maxDist = baseRadius * 0.85f;
        float dist = (float) Math.sqrt(dx * dx + dy * dy);

        if (dist > maxDist) {
            dx = (dx / dist) * maxDist;
            dy = (dy / dist) * maxDist;
        }

        fingerX = dx;
        fingerY = dy;

        float normX = Math.max(-1.0f, Math.min(1.0f, dx / maxDist));
        float normY = Math.max(-1.0f, Math.min(1.0f, dy / maxDist));

        if (listener != null) {
            listener.onValueChanged(normX, normY);
        }

        invalidate();
    }

    private void resetJoystick() {
        isPressed = false;
        activePointerId = -1;
        fingerX = 0f;
        fingerY = 0f;

        if (listener != null) {
            listener.onValueChanged(0f, 0f);
        }

        invalidate();
    }
}
