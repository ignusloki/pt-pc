package org.ignusloki.pt.prototype;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.content.pm.ActivityInfo;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.Bundle;
import android.os.Process;
import android.util.DisplayMetrics;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.RelativeLayout;
import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/** The unchanged SDL controller bridge, with a small optional virtual-controller overlay. */
public final class GameActivity extends SDLActivity {
    private static native void configure(String nativeLibraries, boolean touch);
    private static native void touchStick(int stick, float x, float y);
    private static native void touchButton(int button, boolean pressed);
    private static native void resetTouch();
    private static native void captureForeground(boolean foreground);

    @Override protected String[] getLibraries() { return new String[] { "c++_shared", "SDL3", "main" }; }

    @Override public void loadLibraries() {
        super.loadLibraries();
        configure(getApplicationInfo().nativeLibraryDir, getIntent().getBooleanExtra("touch_controls", false));
    }

    @Override protected String[] getArguments() {
        return new String[] { "--game", getFilesDir().getAbsolutePath() + "/game", "--no-update-check" };
    }

    @Override public void setOrientationBis(int width, int height, boolean resizable, String hint) {
        // The desktop window's resizable flag otherwise makes SDL permit portrait orientation.
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    @Override protected SDLSurface createSDLSurface(Context context) {
        SDLSurface surface = super.createSDLSurface(context);
        DisplayMetrics pixels = new DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(pixels);
        // Keep the phone's aspect ratio, with a true low-resolution Vulkan surface stretched by Android.
        int width = 960;
        int height = Math.max(1, Math.round(width * Math.min(pixels.widthPixels, pixels.heightPixels)
                / (float)Math.max(pixels.widthPixels, pixels.heightPixels)));
        surface.getHolder().setFixedSize(width, height);
        return surface;
    }

    public boolean microphonePermissionGranted() {
        return checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED;
    }

    public int phoneMicrophoneDeviceId() {
        android.media.AudioManager audio = (android.media.AudioManager)getSystemService(Context.AUDIO_SERVICE);
        for (android.media.AudioDeviceInfo input : audio.getDevices(android.media.AudioManager.GET_DEVICES_INPUTS)) {
            if (input.getType() == android.media.AudioDeviceInfo.TYPE_BUILTIN_MIC) return input.getId();
        }
        return -1;
    }

    public long memoryPssKiB() { return android.os.Debug.getPss(); }
    public int thermalStatus() {
        return ((android.os.PowerManager)getSystemService(Context.POWER_SERVICE)).getCurrentThermalStatus();
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (mSurface != null) mSurface.setLayoutParams(new RelativeLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        if (!getIntent().getBooleanExtra("touch_controls", false) || mLayout == null) return;
        addStick(0, RelativeLayout.ALIGN_PARENT_LEFT, "MOVE");
        addStick(1, RelativeLayout.ALIGN_PARENT_RIGHT, "LOOK");
        addButton("Menu", 6, RelativeLayout.ALIGN_PARENT_LEFT, 16, 12);
        addButton("OK / Use", 0, RelativeLayout.ALIGN_PARENT_RIGHT, 16, 12);
        addButton("Back", 1, RelativeLayout.ALIGN_PARENT_RIGHT, 120, 12);
        addButton("Zoom", 8, RelativeLayout.ALIGN_PARENT_RIGHT, 16, 66);
        addButton("Up", 11, RelativeLayout.ALIGN_PARENT_LEFT, 120, 12);
        addButton("Down", 12, RelativeLayout.ALIGN_PARENT_LEFT, 216, 12);
        addButton("Left", 13, RelativeLayout.ALIGN_PARENT_LEFT, 312, 12);
        addButton("Right", 14, RelativeLayout.ALIGN_PARENT_LEFT, 408, 12);
        addButton("Tab", 10, RelativeLayout.ALIGN_PARENT_LEFT, 120, 66);
        mSurface.requestFocus();
    }

    int dp(float value) { return Math.round(value * getResources().getDisplayMetrics().density); }

    void addStick(int stick, int side, String label) {
        Stick view = new Stick(this, stick, label);
        RelativeLayout.LayoutParams position = new RelativeLayout.LayoutParams(dp(170), dp(170));
        position.addRule(side);
        position.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
        position.setMargins(dp(24), 0, dp(24), dp(22));
        mLayout.addView(view, position);
    }

    void addButton(String title, int button, int side, int margin, int top) {
        android.widget.TextView view = new android.widget.TextView(this);
        view.setText(title);
        view.setTextColor(Color.WHITE);
        view.setTextSize(14);
        view.setGravity(android.view.Gravity.CENTER);
        view.setBackgroundColor(0x70444444);
        view.setOnTouchListener((v, event) -> {
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN) { touchButton(button, true); return true; }
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) { touchButton(button, false); return true; }
            return true;
        });
        RelativeLayout.LayoutParams position = new RelativeLayout.LayoutParams(dp(88), dp(44));
        position.addRule(side);
        position.setMargins(dp(margin), dp(top), dp(margin), 0);
        mLayout.addView(view, position);
    }

    @Override protected void onPause() {
        if (!mBrokenLibraries) { resetTouch(); captureForeground(false); }
        super.onPause();
    }

    @Override protected void onResume() {
        super.onResume();
        if (!mBrokenLibraries) captureForeground(true);
    }

    @Override protected void onDestroy() {
        super.onDestroy();
        // Each launch gets clean native engine state; this activity runs in the dedicated :game process.
        Process.killProcess(Process.myPid());
    }

    final class Stick extends View {
        final int stick;
        final String label;
        final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        float x, y;
        int pointer = -1;
        Stick(Context context, int stick, String label) {
            super(context);
            this.stick = stick;
            this.label = label;
        }
        @Override protected void onDraw(Canvas canvas) {
            float cx = getWidth() / 2f, cy = getHeight() / 2f, radius = getWidth() * .42f;
            paint.setColor(0x60404040);
            canvas.drawCircle(cx, cy, radius, paint);
            paint.setColor(0xA0DDDDDD);
            canvas.drawCircle(cx + x * radius, cy + y * radius, radius * .30f, paint);
            paint.setColor(Color.WHITE);
            paint.setTextSize(dp(12));
            paint.setTextAlign(Paint.Align.CENTER);
            canvas.drawText(label, cx, getHeight() - dp(2), paint);
        }
        @Override public boolean onTouchEvent(MotionEvent event) {
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN) pointer = event.getPointerId(event.getActionIndex());
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL ||
                (action == MotionEvent.ACTION_POINTER_UP && event.getPointerId(event.getActionIndex()) == pointer)) {
                pointer = -1;
                x = y = 0;
            } else if (pointer != -1) {
                int index = event.findPointerIndex(pointer);
                if (index >= 0) {
                    float radius = getWidth() * .42f;
                    x = (event.getX(index) - getWidth() / 2f) / radius;
                    y = (event.getY(index) - getHeight() / 2f) / radius;
                    float length = (float)Math.sqrt(x * x + y * y);
                    if (length > 1) { x /= length; y /= length; }
                }
            }
            touchStick(stick, x, y);
            invalidate();
            return true;
        }
    }
}
