package us.lassul.droidnix;

import android.os.Handler;
import android.os.Looper;
import android.view.HapticFeedbackConstants;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ViewConfiguration;
import android.widget.Button;
import android.widget.GridLayout;
import android.widget.PopupWindow;

import com.termux.view.TerminalView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * The rows of extra keys under the terminal, in Termux's format: the
 * extra-keys property is a JSON array of rows, a key is a name ("ESC"),
 * text ("/"), or {key: ..., macro: "CTRL c", display: ..., popup: <key>}.
 * A popup is sent by swiping up. CTRL, ALT, SHIFT and FN apply to the next
 * key, a long press locks them; the arrows and deletes repeat when held.
 */
final class ExtraKeys extends GridLayout {

    static final String DEFAULT_KEYS = "[['ESC','/',{key: '-', popup: '|'},'HOME','UP','END','PGUP'],"
        + " ['TAB','CTRL','ALT','LEFT','DOWN','RIGHT','PGDN']]";

    private static final int TEXT_COLOR = 0xFFFFFFFF;
    private static final int ACTIVE_TEXT_COLOR = 0xFF80DEEA;
    private static final int PRESSED_COLOR = 0xFF7F7F7F;
    /** Upstream's delay between the repeats of a held key. */
    private static final int REPEAT_DELAY = 80;

    private static final Map<String, Integer> KEY_CODES = new HashMap<>();
    private static final List<String> REPEATING = Arrays.asList("UP", "DOWN", "LEFT", "RIGHT", "BKSP", "DEL", "PGUP", "PGDN");
    private static final Map<String, String> ALIASES = new HashMap<>();
    private static final Map<String, String> ARROWS = new HashMap<>();
    private static final Map<String, String> WELL_KNOWN = new HashMap<>();
    private static final Map<String, String> LESS_KNOWN = new HashMap<>();
    private static final Map<String, String> ISO = new HashMap<>();

    static {
        String[] names = {"SPACE", "ESC", "TAB", "HOME", "END", "PGUP", "PGDN", "INS", "DEL", "BKSP", "UP", "LEFT", "RIGHT", "DOWN", "ENTER"};
        int[] codes = {KeyEvent.KEYCODE_SPACE, KeyEvent.KEYCODE_ESCAPE, KeyEvent.KEYCODE_TAB, KeyEvent.KEYCODE_MOVE_HOME,
            KeyEvent.KEYCODE_MOVE_END, KeyEvent.KEYCODE_PAGE_UP, KeyEvent.KEYCODE_PAGE_DOWN, KeyEvent.KEYCODE_INSERT,
            KeyEvent.KEYCODE_FORWARD_DEL, KeyEvent.KEYCODE_DEL, KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_LEFT,
            KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_ENTER};
        for (int i = 0; i < names.length; i++) KEY_CODES.put(names[i], codes[i]);
        for (int i = 1; i <= 12; i++) KEY_CODES.put("F" + i, KeyEvent.KEYCODE_F1 + i - 1);

        put(ALIASES, "ESCAPE", "ESC", "CONTROL", "CTRL", "SHFT", "SHIFT", "RETURN", "ENTER", "FUNCTION", "FN",
            "LT", "LEFT", "RT", "RIGHT", "DN", "DOWN", "PAGEUP", "PGUP", "PAGE_UP", "PGUP", "PAGE UP", "PGUP",
            "PAGE-UP", "PGUP", "PAGEDOWN", "PGDN", "PAGE_DOWN", "PGDN", "PAGE-DOWN", "PGDN", "DELETE", "DEL",
            "BACKSPACE", "BKSP", "BACKSLASH", "\\", "QUOTE", "\"", "APOSTROPHE", "'");
        put(ARROWS, "LEFT", "←", "RIGHT", "→", "UP", "↑", "DOWN", "↓", "-", "―");
        put(WELL_KNOWN, "ENTER", "↲", "TAB", "↹", "BKSP", "⌫", "DEL", "⌦", "DRAWER", "☰", "KEYBOARD", "⌨",
            "PASTE", "⎘", "SCROLL", "⇳");
        put(LESS_KNOWN, "HOME", "⇱", "END", "⇲", "PGUP", "⇑", "PGDN", "⇓");
        put(ISO, "CTRL", "⎈", "ALT", "⎇", "ESC", "⎋");
    }

    private static void put(Map<String, String> map, String... pairs) {
        for (int i = 0; i < pairs.length; i += 2) map.put(pairs[i], pairs[i + 1]);
    }

    /** What the buttons show for extra-keys-style. */
    private static Map<String, String> displayMap(String style) {
        Map<String, String> map = new HashMap<>();
        if (style.equals("none")) return map;
        map.putAll(ARROWS);
        if (style.equals("arrows-only")) return map;
        map.putAll(WELL_KNOWN);
        if (style.equals("arrows-all") || style.equals("all")) map.putAll(LESS_KNOWN);
        if (style.equals("all")) map.putAll(ISO);
        return map;
    }

    /** A key: one key, or the keys of a macro, and the text of its button. */
    private static final class Key {
        final String[] keys;
        final boolean macro;
        final String display;
        final Key popup;

        Key(Object config, Map<String, String> displayMap) throws JSONException {
            JSONObject object = config instanceof JSONObject ? (JSONObject) config : new JSONObject().put("key", config);
            if (object.has("key") == object.has("macro"))
                throw new JSONException("a key needs either key or macro: " + object);
            macro = object.has("macro");
            keys = macro ? object.getString("macro").split(" ") : new String[]{object.getString("key")};
            for (int i = 0; i < keys.length; i++) keys[i] = ALIASES.getOrDefault(keys[i], keys[i]);
            if (object.has("display")) {
                display = object.getString("display");
            } else {
                StringBuilder text = new StringBuilder();
                for (String key : keys) text.append(text.length() > 0 ? " " : "").append(displayMap.getOrDefault(key, key));
                display = text.toString();
            }
            popup = object.has("popup") ? new Key(object.get("popup"), displayMap) : null;
        }
    }

    /** CTRL, ALT, SHIFT or FN: active for the next key, or locked. */
    private static final class Modifier {
        boolean active, locked;
        final List<Button> buttons = new ArrayList<>();

        void set(boolean active, boolean locked) {
            this.active = active;
            this.locked = locked;
            for (Button button : buttons) button.setTextColor(active ? ACTIVE_TEXT_COLOR : TEXT_COLOR);
        }
    }

    private final TerminalActivity activity;
    private final Map<String, Modifier> modifiers = new HashMap<>();
    private final Handler handler = new Handler(Looper.getMainLooper());
    private Runnable longPress;
    private boolean longPressed;
    private PopupWindow popup;
    private boolean allCaps;

    ExtraKeys(TerminalActivity activity) {
        super(activity);
        this.activity = activity;
        for (String name : new String[]{"CTRL", "ALT", "SHIFT", "FN"}) modifiers.put(name, new Modifier());
    }

    /** Shows the keys of the extra-keys property; returns the number of rows. */
    int load(String json, String style, boolean allCaps) throws JSONException {
        this.allCaps = allCaps;
        Map<String, String> displayMap = displayMap(style);
        JSONArray rows = new JSONArray(json);
        List<Key[]> keys = new ArrayList<>();
        int columns = 0;
        for (int row = 0; row < rows.length(); row++) {
            JSONArray array = rows.getJSONArray(row);
            Key[] rowKeys = new Key[array.length()];
            for (int column = 0; column < array.length(); column++) rowKeys[column] = new Key(array.get(column), displayMap);
            keys.add(rowKeys);
            columns = Math.max(columns, rowKeys.length);
        }

        removeAllViews();
        for (Modifier modifier : modifiers.values()) {
            modifier.buttons.clear();
            modifier.set(false, false);
        }
        setRowCount(Math.max(keys.size(), 1));
        setColumnCount(Math.max(columns, 1));
        for (int row = 0; row < keys.size(); row++) {
            for (int column = 0; column < keys.get(row).length; column++) {
                Key key = keys.get(row)[column];
                Button button = button(key.display);
                Modifier modifier = modifier(key);
                if (modifier != null) modifier.buttons.add(button);
                button.setOnTouchListener((view, event) -> onTouch(button, key, event));
                LayoutParams params = new LayoutParams(spec(row, FILL, 1f), spec(column, FILL, 1f));
                params.width = 0;
                params.height = 0;
                addView(button, params);
            }
        }
        return keys.size();
    }

    private Button button(String text) {
        Button button = new Button(getContext(), null, android.R.attr.buttonBarButtonStyle);
        button.setText(text);
        button.setAllCaps(allCaps);
        button.setTextColor(TEXT_COLOR);
        button.setPadding(0, 0, 0, 0);
        button.setMinWidth(0);
        button.setMinimumWidth(0);
        button.setMinHeight(0);
        button.setMinimumHeight(0);
        return button;
    }

    private Modifier modifier(Key key) {
        return key.macro ? null : modifiers.get(key.keys[0]);
    }

    /** Whether a modifier is active; reading it ends it unless it is locked. */
    boolean read(String name) {
        Modifier modifier = modifiers.get(name);
        if (!modifier.active) return false;
        if (!modifier.locked) modifier.set(false, false);
        return true;
    }

    private boolean onTouch(Button button, Key key, MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                button.setBackgroundColor(PRESSED_COLOR);
                longPressed = false;
                Modifier modifier = modifier(key);
                if (modifier != null) {
                    startLongPress(() -> {
                        longPressed = true;
                        modifier.set(!modifier.active, !modifier.active);
                    });
                } else if (!key.macro && REPEATING.contains(key.keys[0])) {
                    startLongPress(new Runnable() {
                        @Override
                        public void run() {
                            longPressed = true;
                            press(key);
                            handler.postDelayed(this, REPEAT_DELAY);
                        }
                    });
                }
                return true;
            case MotionEvent.ACTION_MOVE:
                if (key.popup != null && popup == null && event.getY() < 0) {
                    stopLongPress();
                    button.setBackgroundColor(0);
                    showPopup(button, key.popup);
                } else if (popup != null && event.getY() >= 0) {
                    dismissPopup();
                    button.setBackgroundColor(PRESSED_COLOR);
                }
                return true;
            case MotionEvent.ACTION_UP:
                button.setBackgroundColor(0);
                stopLongPress();
                if (popup != null) {
                    dismissPopup();
                    click(key.popup);
                } else if (!longPressed) {
                    button.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
                    click(key);
                }
                return true;
            case MotionEvent.ACTION_CANCEL:
                button.setBackgroundColor(0);
                stopLongPress();
                if (popup != null) dismissPopup();
                return true;
            default:
                return true;
        }
    }

    private void startLongPress(Runnable runnable) {
        stopLongPress();
        longPress = runnable;
        handler.postDelayed(runnable, ViewConfiguration.getLongPressTimeout());
    }

    private void stopLongPress() {
        if (longPress != null) handler.removeCallbacks(longPress);
        longPress = null;
    }

    private void showPopup(Button anchor, Key key) {
        Button button = button(key.display);
        button.setWidth(anchor.getWidth());
        button.setHeight(anchor.getHeight());
        button.setBackgroundColor(PRESSED_COLOR);
        popup = new PopupWindow(button, LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
        popup.showAsDropDown(anchor, 0, -2 * anchor.getHeight());
    }

    private void dismissPopup() {
        popup.dismiss();
        popup = null;
    }

    private void click(Key key) {
        Modifier modifier = modifier(key);
        if (modifier != null) modifier.set(!modifier.active, false);
        else press(key);
    }

    /** Sends a key, or a macro's keys with the modifiers in it applied to the key after them. */
    private void press(Key key) {
        boolean ctrl = false, alt = false, shift = false, fn = false;
        for (String name : key.keys) {
            if (key.macro && name.equals("CTRL")) ctrl = true;
            else if (key.macro && name.equals("ALT")) alt = true;
            else if (key.macro && name.equals("SHIFT")) shift = true;
            else if (key.macro && name.equals("FN")) fn = true;
            else {
                send(name, ctrl, alt, shift, fn);
                ctrl = alt = shift = fn = false;
            }
        }
    }

    private void send(String name, boolean ctrl, boolean alt, boolean shift, boolean fn) {
        TerminalView terminal = activity.terminalView;
        switch (name) {
            case "KEYBOARD": activity.toggleKeyboard(); return;
            case "DRAWER": activity.toggleDrawer(); return;
            case "PASTE": activity.paste(); return;
            case "SCROLL":
                if (terminal.mEmulator != null) terminal.mEmulator.toggleAutoScrollDisabled();
                return;
        }
        Integer keyCode = KEY_CODES.get(name);
        if (keyCode != null) {
            int meta = (ctrl ? KeyEvent.META_CTRL_ON | KeyEvent.META_CTRL_LEFT_ON : 0)
                | (alt ? KeyEvent.META_ALT_ON | KeyEvent.META_ALT_LEFT_ON : 0)
                | (shift ? KeyEvent.META_SHIFT_ON | KeyEvent.META_SHIFT_LEFT_ON : 0)
                | (fn ? KeyEvent.META_FUNCTION_ON : 0);
            terminal.onKeyDown(keyCode, new KeyEvent(0, 0, KeyEvent.ACTION_UP, keyCode, 0, meta));
        } else {
            boolean ctrlDown = ctrl, altDown = alt;
            name.codePoints().forEach(codePoint ->
                terminal.inputCodePoint(TerminalView.KEY_EVENT_SOURCE_VIRTUAL_KEYBOARD, codePoint, ctrlDown, altDown));
        }
    }

}
