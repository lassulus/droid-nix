package us.lassul.droidnix;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.BroadcastReceiver;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.content.SharedPreferences;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.IBinder;
import android.provider.Settings;
import android.system.ErrnoException;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.ContextMenu;
import android.view.Gravity;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.ArrayAdapter;
import android.widget.EditText;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.drawerlayout.widget.DrawerLayout;

import com.termux.terminal.TerminalColors;
import com.termux.terminal.TerminalEmulator;
import com.termux.terminal.TerminalSession;
import com.termux.terminal.TextStyle;
import com.termux.view.TerminalView;

import org.json.JSONException;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Properties;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** The terminal: shows the service's sessions in a TerminalView, with a drawer to switch between them. */
public final class TerminalActivity extends Activity implements ServiceConnection {

    static final String EXTRA_FAILSAFE = "us.lassul.droidnix.app.failsafe_session";
    /**
     * From droid-nix-reload-settings: reload droid-nix.properties, the colors and
     * the font. With the extra ACTION_RELOAD=storage, from droid-nix-setup-storage:
     * ask for the storage permission and make ~/storage.
     */
    static final String ACTION_RELOAD = "us.lassul.droidnix.app.reload_style";

    private static final int REQUEST_NOTIFICATIONS = 0;
    private static final int REQUEST_STORAGE = 1;

    static final Pattern URL = Pattern.compile("[A-Za-z][A-Za-z0-9+.-]*://\\S+");

    TerminalView terminalView;
    DrawerLayout drawer;
    ExtraKeys extraKeys;
    Props props = new Props();
    final TerminalClient client = new TerminalClient(this);
    TerminalService service;
    private ArrayAdapter<TerminalSession> sessionList;
    private ListView sessionListView;
    private SharedPreferences preferences;
    private int fontSize;

    private final BroadcastReceiver reloadReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if ("storage".equals(intent.getStringExtra(ACTION_RELOAD))) requestStorage();
            else reload();
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_terminal);
        drawer = findViewById(R.id.drawer);
        terminalView = findViewById(R.id.terminal);
        terminalView.setTerminalViewClient(client);
        registerForContextMenu(terminalView);
        extraKeys = new ExtraKeys(this);
        ((ViewGroup) findViewById(R.id.content)).addView(extraKeys, ViewGroup.LayoutParams.MATCH_PARENT, 0);
        loadProps();
        if (props.getBoolean("hide-soft-keyboard-on-startup", false))
            getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN
                | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);

        // Edge to edge: keep the terminal clear of the system bars, cutouts and the keyboard.
        View content = findViewById(R.id.content);
        View drawerPanel = findViewById(R.id.drawer_panel);
        ViewCompat.setOnApplyWindowInsetsListener(drawer, (view, insets) -> {
            Insets bars = insets.getInsets(WindowInsetsCompat.Type.systemBars()
                | WindowInsetsCompat.Type.displayCutout() | WindowInsetsCompat.Type.ime());
            content.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            drawerPanel.setPadding(bars.left, bars.top, 0, bars.bottom);
            return WindowInsetsCompat.CONSUMED;
        });

        preferences = getPreferences(MODE_PRIVATE);
        fontSize = preferences.getInt("font_size",
            Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 12, getResources().getDisplayMetrics())));
        terminalView.setTextSize(fontSize);
        setKeepScreenOn(preferences.getBoolean("keep_screen_on", false));

        sessionListView = findViewById(R.id.sessions);
        findViewById(R.id.toggle_keyboard).setOnClickListener(v -> {
            toggleKeyboard();
            drawer.closeDrawers();
        });
        findViewById(R.id.toggle_keyboard).setOnLongClickListener(v -> {
            toggleExtraKeys();
            drawer.closeDrawers();
            return true;
        });
        findViewById(R.id.new_session).setOnClickListener(v -> newSession(false));
        findViewById(R.id.new_session).setOnLongClickListener(v -> {
            newSession(true);
            return true;
        });

        // Notifications (the sessions, Termux:API) need a runtime permission.
        if (savedInstanceState == null) requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQUEST_NOTIFICATIONS);

        registerReceiver(reloadReceiver, new IntentFilter(ACTION_RELOAD), RECEIVER_NOT_EXPORTED);

        Intent intent = new Intent(this, TerminalService.class);
        startService(intent);
        bindService(intent, this, 0);
    }

    @Override
    public void onServiceConnected(ComponentName name, IBinder binder) {
        service = ((TerminalService.Binder) binder).service();
        service.setActivity(this);

        sessionList = new ArrayAdapter<TerminalSession>(this, android.R.layout.simple_list_item_activated_1, service.sessions) {
            @Override
            public View getView(int position, View convertView, ViewGroup parent) {
                TextView view = (TextView) super.getView(position, convertView, parent);
                TerminalSession session = getItem(position);
                StringBuilder text = new StringBuilder("[" + (position + 1) + "]");
                if (session.mSessionName != null) text.append(' ').append(session.mSessionName);
                if (session.getTitle() != null) text.append(' ').append(session.getTitle());
                view.setText(text);
                view.setPaintFlags(session.isRunning()
                    ? view.getPaintFlags() & ~Paint.STRIKE_THRU_TEXT_FLAG
                    : view.getPaintFlags() | Paint.STRIKE_THRU_TEXT_FLAG);
                return view;
            }
        };
        sessionListView.setAdapter(sessionList);
        sessionListView.setOnItemClickListener((parent, view, position, id) -> {
            showSession(service.sessions.get(position));
            drawer.closeDrawers();
        });
        sessionListView.setOnItemLongClickListener((parent, view, position, id) -> {
            renameSession(service.sessions.get(position));
            return true;
        });

        reload();
        if (!handleIntent(getIntent())) {
            if (service.sessions.isEmpty()) newSession(false);
            else showSession(service.current != null ? service.current : service.sessions.get(0));
        }
        if (!props.getBoolean("hide-soft-keyboard-on-startup", false)) showKeyboard();
    }

    @Override
    public void onServiceDisconnected(ComponentName name) {
        finish();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        if (service != null) handleIntent(intent);
    }

    /** The launcher shortcuts: a new session (ACTION_RUN) or a failsafe one. */
    private boolean handleIntent(Intent intent) {
        if (intent.getBooleanExtra(EXTRA_FAILSAFE, false)) newSession(true);
        else if (Intent.ACTION_RUN.equals(intent.getAction())) newSession(false);
        else return false;
        setIntent(new Intent());
        return true;
    }

    @Override
    protected void onStart() {
        super.onStart();
        terminalView.setTerminalCursorBlinkerState(true, true);
    }

    @Override
    protected void onStop() {
        super.onStop();
        terminalView.setTerminalCursorBlinkerState(false, true);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        unregisterReceiver(reloadReceiver);
        if (service != null) service.setActivity(null);
        unbindService(this);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        // The notification posted before the grant is not shown.
        if (requestCode == REQUEST_NOTIFICATIONS && service != null) service.updateNotification();
        if (requestCode == REQUEST_STORAGE && hasStorage()) setupStorage();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == REQUEST_STORAGE && hasStorage()) setupStorage();
    }

    private boolean hasStorage() {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.R ? Environment.isExternalStorageManager()
            : checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    /** All files access from Android 11 on, so that the shells see the shared storage as a file system. */
    void requestStorage() {
        if (hasStorage())
            setupStorage();
        else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R)
            startActivityForResult(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.parse("package:" + getPackageName())), REQUEST_STORAGE);
        else
            requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE,
                Manifest.permission.WRITE_EXTERNAL_STORAGE}, REQUEST_STORAGE);
    }

    private void setupStorage() {
        try {
            Installer.setupStorage(this);
        } catch (IOException | ErrnoException e) {
            Toast.makeText(this, "~/storage: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        if (drawer.isDrawerOpen(Gravity.START)) drawer.closeDrawers();
        else super.onBackPressed();
    }

    void newSession(boolean failsafe) {
        if (failsafe) showSession(service.addSession(true));
        else Installer.installIfNeeded(this, () -> showSession(service.addSession(false)));
        drawer.closeDrawers();
    }

    void showSession(TerminalSession session) {
        service.current = session;
        terminalView.attachSession(session);
        terminalView.requestFocus();
        updateSessionList();
        updateBackground();
    }

    /** Switches to the next or previous session. */
    void switchSession(boolean next) {
        int count = service.sessions.size();
        int index = service.sessions.indexOf(terminalView.getCurrentSession());
        showSession(service.sessions.get((index + (next ? 1 : count - 1)) % count));
    }

    void switchSession(int index) {
        if (index < service.sessions.size()) showSession(service.sessions.get(index));
    }

    void updateSessionList() {
        if (sessionList == null) return;
        sessionList.notifyDataSetChanged();
        sessionListView.setItemChecked(service.sessions.indexOf(terminalView.getCurrentSession()), true);
    }

    /** Removes a finished session; the activity ends with the last one. */
    void removeSession(TerminalSession session) {
        int index = service.sessions.indexOf(session);
        service.removeSession(session);
        if (service.sessions.isEmpty()) finish();
        else showSession(service.sessions.get(Math.min(index, service.sessions.size() - 1)));
    }

    void renameSession(TerminalSession session) {
        EditText name = new EditText(this);
        name.setSingleLine();
        name.setText(session.mSessionName);
        new AlertDialog.Builder(this)
            .setTitle(R.string.rename_session)
            .setView(name)
            .setPositiveButton(android.R.string.ok, (dialog, which) -> {
                String text = name.getText().toString().trim();
                session.mSessionName = text.isEmpty() ? null : text;
                updateSessionList();
            })
            .setNegativeButton(android.R.string.cancel, null)
            .show();
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

    void toggleKeyboard() {
        // enable/disable: the keyboard stays away, also on a tap, until toggled again.
        if (props.is("soft-keyboard-toggle-behaviour", "enable/disable")) {
            int flag = WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM;
            if ((getWindow().getAttributes().flags & flag) == 0) {
                getWindow().addFlags(flag);
            } else {
                getWindow().clearFlags(flag);
                showKeyboard();
            }
            return;
        }
        WindowInsetsCompat insets = ViewCompat.getRootWindowInsets(terminalView);
        if (insets != null && insets.isVisible(WindowInsetsCompat.Type.ime()))
            WindowCompat.getInsetsController(getWindow(), terminalView).hide(WindowInsetsCompat.Type.ime());
        else
            showKeyboard();
    }

    void toggleExtraKeys() {
        boolean show = extraKeys.getVisibility() != View.VISIBLE;
        preferences.edit().putBoolean("extra_keys", show).apply();
        extraKeys.setVisibility(show ? View.VISIBLE : View.GONE);
    }

    void toggleDrawer() {
        if (drawer.isDrawerOpen(Gravity.START)) drawer.closeDrawers();
        else drawer.openDrawer(Gravity.START);
    }

    void paste() {
        client.onPasteTextFromClipboard(terminalView.getCurrentSession());
    }

    private void setKeepScreenOn(boolean on) {
        if (on) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    // The context menu: from the text selection's "More", Ctrl+Alt+M or a right click.

    @Override
    public void onCreateContextMenu(ContextMenu menu, View view, ContextMenu.ContextMenuInfo menuInfo) {
        menu.add(Menu.NONE, R.string.select_url, Menu.NONE, R.string.select_url);
        menu.add(Menu.NONE, R.string.share_transcript, Menu.NONE, R.string.share_transcript);
        menu.add(Menu.NONE, R.string.reset_terminal, Menu.NONE, R.string.reset_terminal);
        menu.add(Menu.NONE, R.string.kill_process, Menu.NONE, R.string.kill_process)
            .setEnabled(terminalView.getCurrentSession() != null && terminalView.getCurrentSession().isRunning());
        menu.add(Menu.NONE, R.string.keep_screen_on, Menu.NONE, R.string.keep_screen_on)
            .setCheckable(true).setChecked(preferences.getBoolean("keep_screen_on", false));
    }

    @Override
    public boolean onContextItemSelected(MenuItem item) {
        TerminalSession session = terminalView.getCurrentSession();
        if (session == null || session.getEmulator() == null) return true;
        int id = item.getItemId();
        if (id == R.string.select_url) {
            selectUrl();
        } else if (id == R.string.share_transcript) {
            String transcript = session.getEmulator().getScreen().getTranscriptTextWithoutJoinedLines().trim();
            try {
                startActivity(Intent.createChooser(new Intent(Intent.ACTION_SEND)
                    .setType("text/plain").putExtra(Intent.EXTRA_TEXT, transcript), null));
            } catch (RuntimeException e) {
                Toast.makeText(this, e.getMessage(), Toast.LENGTH_LONG).show();
            }
        } else if (id == R.string.reset_terminal) {
            session.reset();
            terminalView.onScreenUpdated();
        } else if (id == R.string.kill_process) {
            new AlertDialog.Builder(this)
                .setMessage(R.string.kill_process_confirm)
                .setPositiveButton(android.R.string.ok, (dialog, which) -> session.finishIfRunning())
                .setNegativeButton(android.R.string.cancel, null)
                .show();
        } else if (id == R.string.keep_screen_on) {
            boolean on = !item.isChecked();
            preferences.edit().putBoolean("keep_screen_on", on).apply();
            setKeepScreenOn(on);
        }
        return true;
    }

    @Override
    public void onContextMenuClosed(Menu menu) {
        super.onContextMenuClosed(menu);
        terminalView.onContextMenuClosed(menu);
    }

    /** The URLs in the transcript, the latest first: a tap copies one, a long tap opens it. */
    void selectUrl() {
        String transcript = terminalView.mEmulator.getScreen().getTranscriptTextWithFullLinesJoined();
        Set<String> found = new LinkedHashSet<>();
        Matcher matcher = URL.matcher(transcript);
        while (matcher.find()) found.add(matcher.group());
        List<String> urls = new ArrayList<>(found);
        Collections.reverse(urls);
        if (urls.isEmpty()) {
            Toast.makeText(this, R.string.no_urls, Toast.LENGTH_SHORT).show();
            return;
        }
        AlertDialog dialog = new AlertDialog.Builder(this)
            .setTitle(R.string.select_url)
            .setItems(urls.toArray(new String[0]), (d, which) -> {
                getSystemService(ClipboardManager.class).setPrimaryClip(ClipData.newPlainText(null, urls.get(which)));
                Toast.makeText(this, R.string.url_copied, Toast.LENGTH_SHORT).show();
            })
            .show();
        dialog.getListView().setOnItemLongClickListener((parent, view, position, id) -> {
            dialog.dismiss();
            openUrl(urls.get(position));
            return true;
        });
    }

    void openUrl(String url) {
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
        } catch (RuntimeException e) {
            Toast.makeText(this, e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    private void loadProps() {
        try {
            props = new Props(this);
        } catch (IOException e) {
            props = new Props();
            Toast.makeText(this, Props.file(this) + ": " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    /**
     * Loads ~/.droid-nix: droid-nix.properties, colors.properties and font.ttf
     * (modules/terminal.nix).
     */
    void reload() {
        loadProps();

        try {
            int rows = extraKeys.load(props.get("extra-keys", ExtraKeys.DEFAULT_KEYS),
                props.get("extra-keys-style", "default"), props.getBoolean("extra-keys-text-all-caps", true));
            float rowHeight = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 37.5f, getResources().getDisplayMetrics());
            extraKeys.getLayoutParams().height = Math.round(rows * rowHeight * props.getFloat("terminal-toolbar-height", 1));
            extraKeys.setVisibility(preferences.getBoolean("extra_keys", true) ? View.VISIBLE : View.GONE);
            extraKeys.requestLayout();
        } catch (JSONException e) {
            Toast.makeText(this, "extra-keys: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }

        DisplayMetrics metrics = getResources().getDisplayMetrics();
        int horizontal = Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, props.getFloat("terminal-margin-horizontal", 3), metrics));
        int vertical = Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, props.getFloat("terminal-margin-vertical", 0), metrics));
        terminalView.setPadding(horizontal, vertical, horizontal, vertical);
        Integer blinkRate = props.getInteger("terminal-cursor-blink-rate");
        terminalView.setTerminalCursorBlinkerRate(blinkRate != null ? blinkRate : 0);
        terminalView.setTerminalCursorBlinkerState(true, true);

        File directory = Props.file(this).getParentFile();
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
        for (TerminalSession session : service.sessions) {
            if (session.getEmulator() == null) continue;
            session.getEmulator().mColors.reset();
            session.getEmulator().setCursorStyle();
        }

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
