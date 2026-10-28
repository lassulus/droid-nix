package us.lassul.droidnix;

import android.Manifest;
import android.app.Activity;
import android.content.ComponentName;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.SharedPreferences;
import android.graphics.Typeface;
import android.os.Bundle;
import android.os.IBinder;
import android.util.TypedValue;
import android.view.View;
import android.widget.Toast;

import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;

import com.termux.terminal.TerminalColors;
import com.termux.terminal.TerminalEmulator;
import com.termux.terminal.TerminalSession;
import com.termux.terminal.TextStyle;
import com.termux.view.TerminalView;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Properties;

/** The terminal: shows the service's sessions in a TerminalView. */
public final class TerminalActivity extends Activity implements ServiceConnection {

    static final String EXTRA_FAILSAFE = "us.lassul.droidnix.app.failsafe_session";

    TerminalView terminalView;
    final TerminalClient client = new TerminalClient(this);
    TerminalService service;
    private SharedPreferences preferences;
    private int fontSize;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_terminal);
        terminalView = findViewById(R.id.terminal);
        terminalView.setTerminalViewClient(client);

        // Edge to edge: keep the terminal clear of the system bars, cutouts and the keyboard.
        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.content), (view, insets) -> {
            Insets bars = insets.getInsets(WindowInsetsCompat.Type.systemBars()
                | WindowInsetsCompat.Type.displayCutout() | WindowInsetsCompat.Type.ime());
            view.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            return WindowInsetsCompat.CONSUMED;
        });

        preferences = getPreferences(MODE_PRIVATE);
        fontSize = preferences.getInt("font_size",
            Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 12, getResources().getDisplayMetrics())));
        terminalView.setTextSize(fontSize);

        // Notifications (the sessions, Termux:API) need a runtime permission.
        if (savedInstanceState == null) requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, 0);

        Intent intent = new Intent(this, TerminalService.class);
        startService(intent);
        bindService(intent, this, 0);
    }

    @Override
    public void onServiceConnected(ComponentName name, IBinder binder) {
        service = ((TerminalService.Binder) binder).service();
        service.setActivity(this);
        reloadStyle();
        if (getIntent().getBooleanExtra(EXTRA_FAILSAFE, false)) {
            getIntent().removeExtra(EXTRA_FAILSAFE);
            showSession(service.addSession(true));
        } else if (service.sessions.isEmpty()) {
            Installer.installIfNeeded(this, () -> showSession(service.addSession(false)));
        } else {
            showSession(service.current != null ? service.current : service.sessions.get(0));
        }
    }

    @Override
    public void onServiceDisconnected(ComponentName name) {
        finish();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        if (service != null && intent.getBooleanExtra(EXTRA_FAILSAFE, false))
            showSession(service.addSession(true));
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (service != null) service.setActivity(null);
        unbindService(this);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        // The notification posted before the grant is not shown.
        if (service != null) service.updateNotification();
    }

    void showSession(TerminalSession session) {
        service.current = session;
        terminalView.attachSession(session);
        terminalView.requestFocus();
        updateBackground();
    }

    /** Removes a finished session; the activity ends with the last one. */
    void removeSession(TerminalSession session) {
        int index = service.sessions.indexOf(session);
        service.removeSession(session);
        if (service.sessions.isEmpty()) finish();
        else showSession(service.sessions.get(Math.min(index, service.sessions.size() - 1)));
    }

    void changeFontSize(boolean larger) {
        fontSize = Math.max(2, fontSize + (larger ? 2 : -2));
        preferences.edit().putInt("font_size", fontSize).apply();
        terminalView.setTextSize(fontSize);
    }

    void showKeyboard() {
        terminalView.requestFocus();
        WindowCompat.getInsetsController(getWindow(), terminalView).show(WindowInsetsCompat.Type.ime());
    }

    /** Loads ~/.droid-nix/colors.properties and font.ttf (modules/terminal.nix). */
    void reloadStyle() {
        File directory = new File(Shell.home(this), ".droid-nix");
        File colorsFile = new File(directory, "colors.properties");
        Properties colors = new Properties();
        try {
            if (colorsFile.exists()) try (InputStream in = new FileInputStream(colorsFile)) {
                colors.load(in);
            }
            TerminalColors.COLOR_SCHEME.updateWith(colors);
        } catch (IOException | IllegalArgumentException e) {
            Toast.makeText(this, colorsFile + ": " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
        for (TerminalSession session : service.sessions)
            if (session.getEmulator() != null) session.getEmulator().mColors.reset();

        File font = new File(directory, "font.ttf");
        try {
            terminalView.setTypeface(font.exists() ? Typeface.createFromFile(font) : Typeface.MONOSPACE);
        } catch (RuntimeException e) {
            Toast.makeText(this, font + ": " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
        updateBackground();
    }

    /** The window behind the system bars gets the terminal's background color. */
    void updateBackground() {
        TerminalEmulator emulator = terminalView.mEmulator;
        int[] colors = emulator != null ? emulator.mColors.mCurrentColors : TerminalColors.COLOR_SCHEME.mDefaultColors;
        getWindow().getDecorView().setBackgroundColor(colors[TextStyle.COLOR_INDEX_BACKGROUND]);
        terminalView.invalidate();
    }

}
