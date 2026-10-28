# Termux:API

The app has [Termux:API](https://wiki.termux.com/wiki/Termux:API) built in;
there is no separate app to install. To get the `termux-*` commands, add to
`~/.config/droid-nix/droid-nix.nix`:

```nix
  android-integration.termux-api.enable = true;
```

and run `droid-nix switch` (20 seconds on a Pixel 8a). The commands are
those of [termux-api-package](https://github.com/termux/termux-api-package).

## Notification with a button

```sh
termux-notification --id demo -t droid-nix -c "Tap the button" \
  --button1 "Write file" --button1-action "date >> ~/button.log"
```

The notification shows up with a **Write file** button. Tapping it runs the
command in droid-nix's environment; `~/button.log` then contains the date.
`termux-notification-remove demo` removes it. Notifications need the
notification permission that the app asks for on the first start (Settings,
Apps, droid-nix, Notifications if you denied it).

## Battery

```sh
termux-battery-status
```

prints JSON:

```json
{
  "present": true,
  "technology": "Li-ion",
  "health": "OVERHEAT",
  "plugged": "PLUGGED_AC",
  "status": "NOT_CHARGING",
  "temperature": 33.1,
  "percentage": 77,
  ...
}
```

## Clipboard

```sh
echo hello from droid-nix | termux-clipboard-set
termux-clipboard-get
```

## Location

```sh
termux-location
```

The first call asks "Allow droid-nix to access this device's location?" and
prints

```json
{
  "error": "Please grant the following permission to use this command: android.permission.ACCESS_FINE_LOCATION"
}
```

Choose **While using the app** and run it again. Location must be turned on
in Android's quick settings. Without a GPS fix (indoors, or with location
off) `termux-location` prints nothing after 30 seconds.
`termux-location -p network -r last` returns the last known location at
once, or `"Failed to get location"` if there is none.

Other commands that need a runtime permission (camera, microphone,
contacts, SMS, call log, phone) work the same way: the first call asks, then
run it again.

## Other apps

Other apps (Tasker, ...) can run commands in droid-nix through the
RUN_COMMAND intent, see
[RUN_COMMAND in the README](../README.md#run_command-tasker-and-other-apps).
