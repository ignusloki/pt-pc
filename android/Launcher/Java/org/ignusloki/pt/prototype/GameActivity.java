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
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.os.SystemClock;
import android.media.AudioAttributes;
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
    private static native void menuAction(int action);
    private static native void captureForeground(boolean foreground);

    private static final int MENU = 0, ACCEPT = 1, BACK = 2, ZOOM = 3,
            UP = 4, DOWN = 5, LEFT = 6, RIGHT = 7, COLUMN = 8, INVERT_Y = 9, INVERT_X = 10;
    private final java.util.ArrayList<TouchControl> controls = new java.util.ArrayList<>();
    private final java.util.ArrayList<Stick> sticks = new java.util.ArrayList<>();
    private int menuMode; // 0: gameplay, 1: original settings, 2: PC columns, 3: PC tabs
    private Vibrator phoneVibrator;
    private boolean phoneRumbleForeground;
    private int phoneRumbleAmplitude;
    private long phoneRumbleStarted;

    private synchronized Vibrator getPhoneVibrator() {
        if (phoneVibrator == null) {
            if (android.os.Build.VERSION.SDK_INT >= 31) {
                VibratorManager manager = (VibratorManager)getSystemService(Context.VIBRATOR_MANAGER_SERVICE);
                if (manager != null) phoneVibrator = manager.getDefaultVibrator();
            } else {
                phoneVibrator = (Vibrator)getSystemService(Context.VIBRATOR_SERVICE);
            }
        }
        return phoneVibrator;
    }

    public synchronized boolean phoneRumbleSupported() {
        Vibrator vibrator = getPhoneVibrator();
        return checkSelfPermission(Manifest.permission.VIBRATE) == PackageManager.PERMISSION_GRANTED
                && vibrator != null && vibrator.hasVibrator();
    }

    public synchronized boolean rumblePhone(int low, int high) {
        if (!phoneRumbleSupported()) return false;
        int level = Math.max(0, Math.min(65535, Math.max(low, high)));
        if (!phoneRumbleForeground || level == 0) {
            if (phoneRumbleAmplitude != 0) phoneVibrator.cancel();
            phoneRumbleAmplitude = 0;
            return true;
        }
        // Adapt the two original controller motors to the phone's actuator. Keep SDL's bounded
        // one-second rumble duration; its refresh and stop calls follow the authored motion voices.
        int amplitude = phoneVibrator.hasAmplitudeControl() ? Math.max(1, (level + 128) / 257)
                : VibrationEffect.DEFAULT_AMPLITUDE;
        long now = SystemClock.uptimeMillis();
        if (amplitude == phoneRumbleAmplitude && now - phoneRumbleStarted < 400) return true;
        try {
            AudioAttributes attributes = new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build();
            phoneVibrator.vibrate(VibrationEffect.createOneShot(1000, amplitude), attributes);
            phoneRumbleAmplitude = amplitude;
            phoneRumbleStarted = now;
            return true;
        } catch (SecurityException unavailable) {
            android.util.Log.w("PT", "Phone vibration unavailable", unavailable);
            return false;
        }
    }

    private synchronized void setPhoneRumbleForeground(boolean foreground) {
        phoneRumbleForeground = foreground;
        if (!foreground && phoneVibrator != null) {
            phoneVibrator.cancel();
            phoneRumbleAmplitude = 0;
        }
    }

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
        addButton(MENU, 6, 9);
        addButton(ACCEPT, 0, 0);
        addButton(BACK, -1, 1);
        addButton(ZOOM, 8, -1);
        addButton(UP, -1, 2);
        addButton(DOWN, -1, 3);
        addButton(LEFT, -1, 4);
        addButton(RIGHT, -1, 5);
        addButton(COLUMN, -1, 6);
        addButton(INVERT_Y, -1, 7);
        addButton(INVERT_X, -1, 8);
        applyTouchLayout();
        mSurface.requestFocus();
    }

    public void setTouchMenuMode(int mode) {
        runOnUiThread(() -> {
            if (mode == menuMode) return;
            resetTouch();
            for (Stick stick : sticks) stick.reset();
            menuMode = mode;
            applyTouchLayout();
        });
    }

    int dp(float value) { return Math.round(value * getResources().getDisplayMetrics().density); }

    void addStick(int stick, int side, String label) {
        Stick view = new Stick(this, stick, label);
        sticks.add(view);
        RelativeLayout.LayoutParams position = new RelativeLayout.LayoutParams(dp(170), dp(170));
        position.addRule(side);
        position.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
        position.setMargins(dp(24), 0, dp(24), dp(22));
        mLayout.addView(view, position);
    }

    void addButton(int kind, int button, int action) {
        TouchControl view = new TouchControl(this, kind, button, action);
        view.setTextColor(Color.WHITE);
        view.setGravity(android.view.Gravity.CENTER);
        view.setBackgroundColor(0xA0404040);
        view.setOnTouchListener((v, event) -> {
            int eventAction = event.getActionMasked();
            if (eventAction == MotionEvent.ACTION_DOWN) {
                if (menuMode == 0 || kind == ZOOM) {
                    view.downButton = button;
                    touchButton(button, true);
                } else menuAction(view.action);
                view.setAlpha(.7f);
                return true;
            }
            if (eventAction == MotionEvent.ACTION_UP || eventAction == MotionEvent.ACTION_CANCEL) {
                if (view.downButton >= 0) touchButton(view.downButton, false);
                view.downButton = -1;
                view.setAlpha(1f);
                return true;
            }
            return true;
        });
        controls.add(view);
        mLayout.addView(view);
    }

    void applyTouchLayout() {
        boolean menu = menuMode != 0, original = menuMode == 1;
        for (Stick stick : sticks) stick.setVisibility(menu ? View.GONE : View.VISIBLE);
        for (TouchControl view : controls) {
            int kind = view.kind;
            boolean visible = menu || kind == MENU || kind == ACCEPT || kind == ZOOM;
            if (kind == COLUMN) visible = menuMode >= 2;
            if (kind == INVERT_Y || kind == INVERT_X) visible = original;
            if (kind == ZOOM && menuMode >= 2) visible = false;
            view.setVisibility(visible ? View.VISIBLE : View.GONE);
            view.setAlpha(1f);
            view.downButton = -1;
            String title;
            switch (kind) {
                case MENU: title = menu ? "Resume" : "Menu"; break;
                case ACCEPT: title = original ? "PC\nSettings" : menu ? "OK" : "OK / Use"; break;
                case BACK: title = "Back"; break;
                case ZOOM: title = "Zoom"; break;
                case UP: title = original ? "Language\n▲" : "Up"; break;
                case DOWN: title = original ? "Language\n▼" : "Down"; break;
                case LEFT: title = original ? "Bright −" : "Left"; break;
                case RIGHT: title = original ? "Bright +" : "Right"; break;
                case COLUMN: title = menuMode == 3 ? "Tab" : "Column"; break;
                case INVERT_Y: title = "Flip Y"; break;
                default: title = "Flip X";
            }
            view.setText(title);
            view.setTextSize(menu ? 12 : 14);
            int side = kind == MENU || kind >= UP && kind <= COLUMN
                    ? RelativeLayout.ALIGN_PARENT_LEFT : RelativeLayout.ALIGN_PARENT_RIGHT;
            int row = kind == UP || kind == BACK ? 1 : kind == DOWN || kind == ZOOM ? 2
                    : kind == LEFT || kind == INVERT_Y ? 3 : kind == RIGHT || kind == INVERT_X ? 4 : kind == COLUMN ? 5 : 0;
            RelativeLayout.LayoutParams position = new RelativeLayout.LayoutParams(dp(menu ? 56 : 88), dp(44));
            position.addRule(side);
            int margin = dp(menu ? 8 : 16), top = dp(menu ? 8 + 52 * row : kind == ZOOM ? 66 : 12);
            position.setMargins(margin, top, margin, 0);
            view.setLayoutParams(position);
        }
    }

    final class TouchControl extends android.widget.TextView {
        final int kind, button, action;
        int downButton = -1;
        TouchControl(Context context, int kind, int button, int action) {
            super(context);
            this.kind = kind;
            this.button = button;
            this.action = action;
        }
    }

    @Override protected void onPause() {
        setPhoneRumbleForeground(false);
        if (!mBrokenLibraries) {
            resetTouch();
            for (Stick stick : sticks) stick.reset();
            captureForeground(false);
        }
        super.onPause();
    }

    @Override protected void onResume() {
        super.onResume();
        setPhoneRumbleForeground(true);
        if (!mBrokenLibraries) captureForeground(true);
    }

    @Override protected void onDestroy() {
        setPhoneRumbleForeground(false);
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
        void reset() {
            pointer = -1;
            x = y = 0;
            touchStick(stick, 0, 0);
            invalidate();
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
