package com.dantesinferno.game;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

/**
 * Cleanly relaunches the application from an isolated process (:restart).
 *
 * An Android process cannot exec itself or cleanly purge low-level native hooks
 * (such as AdrenoTools Turnip hooks or Vulkan driver states) once loaded into memory.
 * By running in a separate process (:restart), this activity survives the
 * main process termination, waits 750ms for the old process to fully exit, and
 * then starts TitleActivity afresh with a completely clean environment.
 */
public class RestartActivity extends Activity {

    private static final String TAG = "RestartActivity";
    private static final long RESTART_DELAY_MS = 750;

    /**
     * Trigger an isolated process restart.
     * Starts RestartActivity in :restart, terminates caller activity, and exits process.
     */
    public static void restart(Context context) {
        if (context == null) return;
        try {
            Intent intent = new Intent(context, RestartActivity.class);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            context.startActivity(intent);
            if (context instanceof Activity) {
                ((Activity) context).finish();
            }
            Runtime.getRuntime().exit(0);
        } catch (Throwable t) {
            Log.e(TAG, "Failed to launch restart activity: " + t.getMessage(), t);
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.i(TAG, "Isolated restart process initialized; relaunching TitleActivity in " + RESTART_DELAY_MS + "ms");

        new Handler(Looper.getMainLooper()).postDelayed(() -> {
            try {
                Intent intent = new Intent(this, TitleActivity.class);
                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
                startActivity(intent);
            } catch (Throwable t) {
                Log.e(TAG, "Failed to relaunch TitleActivity: " + t.getMessage(), t);
            } finally {
                finish();
                Runtime.getRuntime().exit(0);
            }
        }, RESTART_DELAY_MS);
    }
}
