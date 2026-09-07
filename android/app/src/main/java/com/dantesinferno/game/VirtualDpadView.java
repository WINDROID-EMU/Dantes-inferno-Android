package com.dantesinferno.game;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;

import androidx.annotation.NonNull;

/**
 * Modular virtual D-Pad cross component that can be placed directly in XML layouts.
 * Imported from WINDROID-EMU/Silent-Hill-Downpour-port-android.
 */
public class VirtualDpadView extends View {

    public interface OnDpadListener {
        void onDpadChanged(boolean up, boolean down, boolean left, boolean right, int hatX, int hatY);
    }

    private Paint basePaint;
    private Paint strokePaint;
    private Paint highlightPaint;
    private Paint arrowPaint;

    private float centerX = 0f;
    private float centerY = 0f;
    private float radius = 0f;
    private float armWidth = 0f;

    private boolean upPressed = false;
    private boolean downPressed = false;
    private boolean leftPressed = false;
    private boolean rightPressed = false;
    private int activePointerId = -1;

    private OnDpadListener listener;

    public VirtualDpadView(Context context) {
        super(context);
        init();
    }

    public VirtualDpadView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public VirtualDpadView(Context context, AttributeSet attrs, int defStyleAttr) {
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

        highlightPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        highlightPaint.setStyle(Paint.Style.FILL);
        highlightPaint.setColor(0xFF58A6FF);

        arrowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        arrowPaint.setColor(Color.WHITE);
        arrowPaint.setTextAlign(Paint.Align.CENTER);
        arrowPaint.setTextSize(14f * density);
        arrowPaint.setFakeBoldText(true);
    }

    public void setOnDpadListener(OnDpadListener l) {
        this.listener = l;
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        centerX = w / 2f;
        centerY = h / 2f;
        radius = Math.min(w, h) / 2f - strokePaint.getStrokeWidth();
        armWidth = radius * 0.38f;
    }

    @Override
    protected void onDraw(@NonNull Canvas canvas) {
        super.onDraw(canvas);

        float cx = centerX;
        float cy = centerY;
        float r = radius;
        float aw = armWidth;

        // Background Cross
        canvas.drawRect(cx - aw, cy - r, cx + aw, cy + r, basePaint);
        canvas.drawRect(cx - r, cy - aw, cx + r, cy + aw, basePaint);

        // Highlight active directions
        if (upPressed) canvas.drawRect(cx - aw, cy - r, cx + aw, cy - aw, highlightPaint);
        if (downPressed) canvas.drawRect(cx - aw, cy + aw, cx + aw, cy + r, highlightPaint);
        if (leftPressed) canvas.drawRect(cx - r, cy - aw, cx - aw, cy + aw, highlightPaint);
        if (rightPressed) canvas.drawRect(cx + aw, cy - aw, cx + r, cy + aw, highlightPaint);

        // Contour Cross Path
        Path crossPath = new Path();
        crossPath.moveTo(cx - aw, cy - aw);
        crossPath.lineTo(cx - aw, cy - r);
        crossPath.lineTo(cx + aw, cy - r);
        crossPath.lineTo(cx + aw, cy - aw);
        crossPath.lineTo(cx + r, cy - aw);
        crossPath.lineTo(cx + r, cy + aw);
        crossPath.lineTo(cx + aw, cy + aw);
        crossPath.lineTo(cx + aw, cy + r);
        crossPath.lineTo(cx - aw, cy + r);
        crossPath.lineTo(cx - aw, cy + aw);
        crossPath.lineTo(cx - r, cy + aw);
        crossPath.lineTo(cx - r, cy - aw);
        crossPath.close();

        canvas.drawPath(crossPath, strokePaint);

        // Draw Directional Arrows
        float textOffset = (arrowPaint.descent() + arrowPaint.ascent()) / 2f;
        drawArrow(canvas, cx, cy - r * 0.60f, upPressed, "▲", textOffset);
        drawArrow(canvas, cx, cy + r * 0.60f, downPressed, "▼", textOffset);
        drawArrow(canvas, cx - r * 0.60f, cy, leftPressed, "◀", textOffset);
        drawArrow(canvas, cx + r * 0.60f, cy, rightPressed, "▶", textOffset);
    }

    private void drawArrow(Canvas canvas, float x, float y, boolean pressed, String arrow, float offset) {
        arrowPaint.setColor(pressed ? Color.BLACK : Color.WHITE);
        canvas.drawText(arrow, x, y - offset, arrowPaint);
    }

    @SuppressLint("ClickableViewAccessibility")
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int actionMasked = event.getActionMasked();
        int actionIndex = event.getActionIndex();

        switch (actionMasked) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                if (activePointerId == -1) {
                    activePointerId = event.getPointerId(actionIndex);
                    updateDpadState(event.getX(actionIndex), event.getY(actionIndex));
                    return true;
                }
                break;
            }

            case MotionEvent.ACTION_MOVE: {
                int pointerIndex = event.findPointerIndex(activePointerId);
                if (pointerIndex != -1) {
                    updateDpadState(event.getX(pointerIndex), event.getY(pointerIndex));
                    return true;
                }
                break;
            }

            case MotionEvent.ACTION_POINTER_UP: {
                if (event.getPointerId(actionIndex) == activePointerId) {
                    resetDpad();
                    return true;
                }
                break;
            }

            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL: {
                resetDpad();
                return true;
            }
        }

        return super.onTouchEvent(event);
    }

    private void updateDpadState(float touchX, float touchY) {
        float dx = touchX - centerX;
        float dy = touchY - centerY;
        float deadzone = radius * 0.18f;

        boolean newUp = (dy < -deadzone) && (Math.abs(dx) <= Math.abs(dy) * 1.5f);
        boolean newDown = (dy > deadzone) && (Math.abs(dx) <= Math.abs(dy) * 1.5f);
        boolean newLeft = (dx < -deadzone) && (Math.abs(dy) <= Math.abs(dx) * 1.5f);
        boolean newRight = (dx > deadzone) && (Math.abs(dy) <= Math.abs(dx) * 1.5f);

        int hatX = 0;
        int hatY = 0;
        if (newLeft) hatX = -1;
        else if (newRight) hatX = 1;
        if (newUp) hatY = -1;
        else if (newDown) hatY = 1;

        if (newUp != upPressed || newDown != downPressed || newLeft != leftPressed || newRight != rightPressed) {
            upPressed = newUp;
            downPressed = newDown;
            leftPressed = newLeft;
            rightPressed = newRight;

            if (listener != null) {
                listener.onDpadChanged(upPressed, downPressed, leftPressed, rightPressed, hatX, hatY);
            }
            invalidate();
        }
    }

    private void resetDpad() {
        activePointerId = -1;
        if (upPressed || downPressed || leftPressed || rightPressed) {
            upPressed = false;
            downPressed = false;
            leftPressed = false;
            rightPressed = false;
            if (listener != null) {
                listener.onDpadChanged(false, false, false, false, 0, 0);
            }
            invalidate();
        }
    }
}
