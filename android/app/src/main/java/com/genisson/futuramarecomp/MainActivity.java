package com.genisson.futuramarecomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.StatFs;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.SeekBar;
import android.widget.TextView;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** The system file picker grants read access; the native VFS only sees the verified private copy. */
public class MainActivity extends Activity implements SurfaceHolder.Callback {
    private static final int PICK_DISC = 10;
    private static final String LOAD_ERROR;
    static {
        String error = null;
        try { System.loadLibrary("futurama_android"); }
        catch (UnsatisfiedLinkError exception) { error = exception.getMessage(); }
        LOAD_ERROR = error;
    }
    private final ExecutorService io = Executors.newSingleThreadExecutor();
    private volatile boolean destroyed;
    private FrameLayout root;
    private LinearLayout launcher;
    private TextView status, gameStatus;
    private ProgressBar progress;
    private Button choose, play;
    private SurfaceView surface;
    private TouchControlsView controls;
    private File disc;
    private boolean ready, started, resumed, surfaceReady, busy, openingGame;

    private static native String nativeValidateIso(String path);
    private native void nativeStart(Surface surface, String disc, String saves);
    private native void nativeSurface(Surface surface);
    private native void nativeStop();
    private native void nativePause(boolean paused);
    private native void nativeKey(int key, boolean down);
    private native void nativeAxes(float lx, float ly, float rx, float ry, float lt, float rt, float hx, float hy);
    private native void nativeTouch(int buttons, float lx, float ly, float rx, float ry);
    private native void nativeLook(float dx, float dy);
    private native void nativeClearTouch();
    private native long nativeControlState();

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        // Some launchers deliver a fresh MAIN intent above the running GameActivity.
        // Discard that duplicate before it can validate the disc and create another surface owner.
        Intent launch = getIntent();
        if (!(this instanceof GameActivity) && !isTaskRoot()
                && Intent.ACTION_MAIN.equals(launch.getAction()) && launch.hasCategory(Intent.CATEGORY_LAUNCHER)) {
            finish();
            return;
        }
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        disc = new File(getFilesDir(), "futurama.iso");
        root = new FrameLayout(this);
        root.setBackgroundColor(Color.rgb(16, 22, 27));
        setContentView(root);
        makeLauncher();
        immersive();
        if (LOAD_ERROR != null) {
            status.setText("The native game library could not load:\n" + LOAD_ERROR);
            choose.setEnabled(false); return;
        }
        // The recompiled core has process-wide state and supports one App lifetime per process.
        // A separate game process gives every session a fresh core without disc or save duplication.
        if (this instanceof GameActivity) {
            ready = disc.isFile();
            if (ready) showGame(); else { status.setText("Install a disc from the launcher first."); }
            return;
        }
        if (getIntent().getBooleanExtra("pickDisc", false)) { pickDisc(); return; }
        if (disc.isFile()) validateExisting();
    }
    private int dp(float value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private TextView text(String value, int size) {
        TextView view = new TextView(this); view.setText(value); view.setTextSize(size);
        view.setTextColor(Color.WHITE); view.setGravity(Gravity.CENTER); return view;
    }
    private void makeLauncher() {
        launcher = new LinearLayout(this); launcher.setOrientation(LinearLayout.VERTICAL);
        launcher.setGravity(Gravity.CENTER); launcher.setPadding(dp(30), dp(14), dp(30), dp(14));
        root.addView(launcher, new FrameLayout.LayoutParams(-1, -1));
        launcher.addView(text("FUTURAMA RECOMPILED", 25));
        TextView detail = text("Native Android port · Xbox NTSC-U / PAL\nSelect your own disc image. No game data is included.", 15);
        detail.setPadding(0, dp(10), 0, dp(10)); launcher.addView(detail);
        status = text("Your disc will be copied into app storage. Keep enough free space for the entire ISO.", 14);
        launcher.addView(status, new LinearLayout.LayoutParams(-1, -2));
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setVisibility(View.GONE); launcher.addView(progress, new LinearLayout.LayoutParams(dp(340), dp(12)));
        choose = new Button(this); choose.setText("Select Xbox disc image");
        choose.setOnClickListener(v -> pickDisc()); launcher.addView(choose);
        play = new Button(this); play.setText("Play installed disc"); play.setVisibility(View.GONE);
        play.setOnClickListener(v -> showGame()); launcher.addView(play);
        launcher.addView(text("Touch controls or a Bluetooth / USB controller · Saves stay in app storage", 12));
    }
    private void setBusy(boolean value, String message) {
        busy = value; status.setText(message); choose.setEnabled(!value);
        play.setEnabled(!value); progress.setIndeterminate(true);
        progress.setVisibility(value ? View.VISIBLE : View.GONE);
    }
    private void ui(Runnable action) { runOnUiThread(() -> { if (!destroyed) action.run(); }); }
    private void validateExisting() {
        setBusy(true, "Checking installed disc…");
        io.execute(() -> {
            String error = nativeValidateIso(disc.getAbsolutePath());
            ui(() -> {
                ready = error.isEmpty(); setBusy(false, ready ? "Installed disc is ready." : "Installed disc is not valid: " + error);
                play.setVisibility(ready ? View.VISIBLE : View.GONE);
                if (ready && resumed) showGame();
            });
        });
    }
    private void pickDisc() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE); intent.setType("*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try { startActivityForResult(intent, PICK_DISC); }
        catch (android.content.ActivityNotFoundException error) { status.setText("No system file picker is available on this device."); }
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_DISC || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        setBusy(true, "Reading selected disc…");
        io.execute(() -> importDisc(uri));
    }
    private void importDisc(Uri uri) {
        File pending = new File(getFilesDir(), "futurama.iso.importing");
        try {
            long total = -1;
            try (Cursor cursor = getContentResolver().query(uri, new String[]{OpenableColumns.SIZE}, null, null, null)) {
                if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) total = cursor.getLong(0);
            }
            if (total > 0 && new StatFs(getFilesDir().getPath()).getAvailableBytes() < total + 64L * 1024 * 1024)
                throw new IOException("Not enough free storage. The complete disc image needs " + (total / (1024 * 1024)) + " MiB plus temporary space.");
            long copied = 0, lastReport = 0;
            try (InputStream raw = getContentResolver().openInputStream(uri)) {
                if (raw == null) throw new IOException("The file provider did not open the selected image.");
                try (BufferedInputStream in = new BufferedInputStream(raw, 1024 * 1024);
                     FileOutputStream file = new FileOutputStream(pending);
                     BufferedOutputStream out = new BufferedOutputStream(file, 1024 * 1024)) {
                    byte[] buffer = new byte[1024 * 1024]; int count;
                    while ((count = in.read(buffer)) != -1) {
                        if (Thread.currentThread().isInterrupted()) throw new IOException("Import cancelled.");
                        out.write(buffer, 0, count); copied += count;
                        long now = android.os.SystemClock.uptimeMillis();
                        if (now - lastReport > 150) {
                            final long bytes = copied, size = total;
                            ui(() -> {
                                progress.setIndeterminate(size <= 0);
                                if (size > 0) { progress.setMax(1000); progress.setProgress((int) Math.min(1000, bytes * 1000 / size)); }
                                status.setText("Copying disc: " + bytes / (1024 * 1024) + " MiB" + (size > 0 ? " / " + size / (1024 * 1024) + " MiB" : ""));
                            });
                            lastReport = now;
                        }
                    }
                    out.flush(); file.getFD().sync();
                }
            }
            ui(() -> { progress.setIndeterminate(true); status.setText("Validating Xbox disc and executable…"); });
            String error = nativeValidateIso(pending.getAbsolutePath());
            if (!error.isEmpty()) throw new IOException(error);
            if (Thread.currentThread().isInterrupted()) throw new IOException("Import cancelled.");
            try { Files.move(pending.toPath(), disc.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING); }
            catch (AtomicMoveNotSupportedException ignored) { Files.move(pending.toPath(), disc.toPath(), StandardCopyOption.REPLACE_EXISTING); }
            ui(() -> { ready = true; setBusy(false, "Disc installed and verified."); play.setVisibility(View.VISIBLE); if (resumed) showGame(); });
        } catch (Exception error) {
            pending.delete();
            ui(() -> { setBusy(false, "Could not import disc: " + error.getMessage()); play.setVisibility(ready ? View.VISIBLE : View.GONE); });
        }
    }
    private void showGame() {
        if (!ready || busy || surface != null) return;
        if (!(this instanceof GameActivity)) {
            if (openingGame) return;
            openingGame = true;
            startActivity(new Intent(this, GameActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT | Intent.FLAG_ACTIVITY_SINGLE_TOP));
            return;
        }
        launcher.setVisibility(View.GONE);
        surface = new SurfaceView(this); surface.getHolder().addCallback(this);
        root.addView(surface, new FrameLayout.LayoutParams(-1, -1));
        controls = new TouchControlsView(this, new TouchControlsView.Sink() {
            public boolean running() { return started && resumed; }
            public long state() { return nativeControlState(); }
            public void touch(int buttons, float lx, float ly) { nativeTouch(buttons,lx,ly,0,0); }
            public void look(float dx, float dy) { nativeLook(dx,dy); }
            public void clearTouch() { nativeClearTouch(); }
            public void settings() { gameMenu(); }
        });
        controls.loadCameraOptions();
        root.addView(controls, new FrameLayout.LayoutParams(-1, -1));
        gameStatus = text("Starting native game…", 14);
        FrameLayout.LayoutParams statusLayout = new FrameLayout.LayoutParams(-2, -2, Gravity.TOP | Gravity.LEFT);
        statusLayout.setMargins(dp(16), dp(8), dp(16), 0); root.addView(gameStatus, statusLayout);
    }
    private void gameMenu() {
        nativePause(true); controls.clear();
        final boolean[] cameraDialog = {false};
        String[] items = {"Continuar", controls.touchVisible() ? "Ocultar controles" : "Mostrar controles", "Ajustar câmera", "Trocar imagem do disco", "Sair"};
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle("Futurama Android").setItems(items, (d, which) -> {
            if (which == 1) controls.setTouchVisible(!controls.touchVisible());
            if (which == 2) { cameraDialog[0] = true; cameraOptions(); }
            if (which == 3) {
                startActivity(new Intent(this, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP).putExtra("pickDisc", true));
                finish();
            }
            if (which == 4) finish();
        }).create();
        dialog.setOnDismissListener(d -> { if (!cameraDialog[0] && started && resumed) nativePause(false); immersive(); }); dialog.show();
    }
    private void cameraOptions() {
        LinearLayout options = new LinearLayout(this); options.setOrientation(LinearLayout.VERTICAL);
        options.setPadding(dp(22), dp(12), dp(22), dp(12));
        TextView sensitivity = new TextView(this);
        SeekBar slider = new SeekBar(this); slider.setMax(165); slider.setProgress(Math.round((controls.sensitivity()-.35f)*100));
        sensitivity.setText(String.format(java.util.Locale.getDefault(), "Sensibilidade: %.2f×", controls.sensitivity()));
        slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar bar, int value, boolean user) { sensitivity.setText(String.format(java.util.Locale.getDefault(), "Sensibilidade: %.2f×", .35f+value/100f)); }
            public void onStartTrackingTouch(SeekBar bar) {}
            public void onStopTrackingTouch(SeekBar bar) {}
        });
        CheckBox horizontal = new CheckBox(this); horizontal.setText("Inverter horizontal"); horizontal.setChecked(controls.invertX());
        CheckBox vertical = new CheckBox(this); vertical.setText("Inverter vertical"); vertical.setChecked(controls.invertY());
        options.addView(sensitivity); options.addView(slider); options.addView(horizontal); options.addView(vertical);
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle("Câmera por arrasto").setView(options)
            .setPositiveButton("Salvar", (d,w) -> controls.setCameraOptions(.35f+slider.getProgress()/100f,horizontal.isChecked(),vertical.isChecked()))
            .setNeutralButton("Padrão", (d,w) -> controls.setCameraOptions(1,false,false))
            .setNegativeButton("Cancelar", null).create();
        dialog.setOnDismissListener(d -> { if (started && resumed) nativePause(false); immersive(); }); dialog.show();
    }
    private void stopGameView() {
        if (this instanceof GameActivity) { finish(); return; }
        if (started) nativeStop(); started = false; surfaceReady = false;
        if (surface != null) surface.getHolder().removeCallback(this);
        root.removeAllViews(); root.addView(launcher); launcher.setVisibility(View.VISIBLE);
        surface = null; controls = null; gameStatus = null;
    }
    @Override public void surfaceCreated(SurfaceHolder holder) { surfaceReady = true; startIfPossible(); }
    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (started) nativeSurface(holder.getSurface());
    }
    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        surfaceReady = false; if (started) nativeSurface(null);
    }
    private void startIfPossible() {
        if (!resumed || !ready || !surfaceReady) return;
        if (!started) {
            File saves = new File(getFilesDir(), "saves"); saves.mkdirs();
            nativeStart(surface.getHolder().getSurface(), disc.getAbsolutePath(), saves.getAbsolutePath()); started = true;
        } else nativeSurface(surface.getHolder().getSurface());
        nativePause(false);
    }
    @Override protected void onResume() { super.onResume(); resumed = true; openingGame = false; immersive(); startIfPossible(); if (started) nativePause(false); }
    @Override protected void onPause() { resumed = false; if (controls != null) controls.clear(); if (started) nativePause(true); super.onPause(); }
    @Override protected void onDestroy() {
        destroyed = true; io.shutdownNow(); if (started) nativeStop(); started = false; super.onDestroy();
        // Only :game exits. The launcher, installed disc and saves remain available for the next session.
        if (this instanceof GameActivity) android.os.Process.killProcess(android.os.Process.myPid());
    }
    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent); setIntent(intent);
        if (!(this instanceof GameActivity) && intent.getBooleanExtra("pickDisc", false) && LOAD_ERROR == null && !busy) pickDisc();
    }
    @Override public void onWindowFocusChanged(boolean focus) { super.onWindowFocusChanged(focus); if (focus) immersive(); else if (controls != null) controls.clear(); }
    private void immersive() {
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) { controller.hide(WindowInsets.Type.systemBars()); controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE); }
        } else getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }
    // Called by the native owner thread; all Android UI work is marshalled to the main thread.
    public void onNativeError(String error) { ui(() -> new AlertDialog.Builder(this).setTitle("Native game error").setMessage(error).setPositiveButton("Return to launcher", (d, w) -> stopGameView()).setCancelable(false).show()); }
    public void onNativeStatus(String message) { ui(() -> { if (gameStatus != null) { gameStatus.setText(message); gameStatus.setVisibility(message.isEmpty() ? View.GONE : View.VISIBLE); } }); }
    private boolean mappedKey(int key) {
        return KeyEvent.isGamepadButton(key) || key == KeyEvent.KEYCODE_BACK || key == KeyEvent.KEYCODE_ENTER || key == KeyEvent.KEYCODE_SPACE || (key >= KeyEvent.KEYCODE_DPAD_UP && key <= KeyEvent.KEYCODE_DPAD_CENTER);
    }
    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if (started && mappedKey(event.getKeyCode()) && (event.getAction() == KeyEvent.ACTION_DOWN || event.getAction() == KeyEvent.ACTION_UP)) {
            nativeKey(event.getKeyCode(), event.getAction() == KeyEvent.ACTION_DOWN); return true;
        }
        return super.dispatchKeyEvent(event);
    }
    private float axis(MotionEvent event, int axis) {
        float value = event.getAxisValue(axis);
        InputDevice device = event.getDevice();
        InputDevice.MotionRange range = device == null ? null : device.getMotionRange(axis, event.getSource());
        return Math.abs(value) <= (range == null ? .12f : range.getFlat()) ? 0 : value;
    }
    @Override public boolean onGenericMotionEvent(MotionEvent event) {
        if (started && (event.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK && event.getAction() == MotionEvent.ACTION_MOVE) {
            InputDevice device = event.getDevice();
            boolean rightIsZ = device == null || device.getMotionRange(MotionEvent.AXIS_Z, event.getSource()) != null;
            nativeAxes(axis(event, MotionEvent.AXIS_X), axis(event, MotionEvent.AXIS_Y), axis(event, rightIsZ ? MotionEvent.AXIS_Z : MotionEvent.AXIS_RX), axis(event, rightIsZ ? MotionEvent.AXIS_RZ : MotionEvent.AXIS_RY),
                Math.max(axis(event, MotionEvent.AXIS_LTRIGGER), axis(event, MotionEvent.AXIS_BRAKE)), Math.max(axis(event, MotionEvent.AXIS_RTRIGGER), axis(event, MotionEvent.AXIS_GAS)),
                axis(event, MotionEvent.AXIS_HAT_X), axis(event, MotionEvent.AXIS_HAT_Y)); return true;
        }
        return super.onGenericMotionEvent(event);
    }
}
