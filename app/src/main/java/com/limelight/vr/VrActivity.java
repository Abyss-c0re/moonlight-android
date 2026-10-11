package com.limelight.vr;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;
import android.widget.Toast;

import com.limelight.R;

/**
 * OpenXR activity. The PC list does not start this yet; a later change adds
 * the button. System overlays pause the activity without tearing the session
 * down. Back, and the in-headset exit added later, return to the 2D app.
 */
public class VrActivity extends Activity {
    private boolean started;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(new View(this));
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        MoonlightXr.nativeRequestExit();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!started) {
            started = true;
            if (!MoonlightXr.nativeStart(this)) {
                Toast.makeText(this, R.string.vr_unavailable, Toast.LENGTH_LONG).show();
                finish();
            }
        }
    }

    @Override
    protected void onPause() {
        // Quest and Pico system UI pause this activity. The OpenXR session
        // stays alive and resumes when the runtime returns to READY.
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        MoonlightXr.nativeShutdown();
        super.onDestroy();
    }

    @SuppressWarnings("unused")
    public void onSessionEnded(String reason) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (!isFinishing()) {
                    finish();
                }
            }
        });
    }
}
