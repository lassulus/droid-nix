# Android APIs

The app has [Termux:API](https://wiki.termux.com/wiki/Termux:API) built in;
there is no separate app to install. To get the commands of
[termux-api-package](https://github.com/termux/termux-api-package), named
`droid-nix-*` instead of `termux-*`, add to
`~/.config/droid-nix/droid-nix.nix`:

```nix
  android-integration.api.enable = true;
```

and run `droid-nix switch`.

Examples:

```sh
droid-nix-notification --id demo -t droid-nix -c "Tap the button" \
  --button1 "Write file" --button1-action "date >> ~/button.log"
droid-nix-battery-status
echo hello | droid-nix-clipboard-set
droid-nix-clipboard-get
droid-nix-location
```

Commands that need a runtime permission (location, camera, microphone,
contacts, SMS, ...) make Android ask for it on the first call and print an
error; allow it and run the command again. `droid-nix-location` also needs
location turned on, and prints nothing if it gets no fix;
`droid-nix-location -p network -r last` returns the last known location.

Other apps (Tasker, ...) can run commands in droid-nix through the
RUN_COMMAND intent, see
[RUN_COMMAND in the README](../README.md#run_command-tasker-and-other-apps).

## adb to the phone itself

Over Android's Wireless debugging, `adb` in droid-nix can connect to the
phone it runs on and run commands as the shell user, which may do more
than an app: `pm`, `settings`, `dumpsys`, `input`, `appops`, ... Add to
`~/.config/droid-nix/droid-nix.nix`:

```nix
  android-integration.adb.enable = true;
```

and run `droid-nix switch`. This installs `adb` (from android-tools) and
`droid-nix-adb-connect`, about 90 MB, half of it the C library that
droid-nix has anyway.

Pair once (needs the developer options and Wi-Fi):

1. In Settings, Developer options, turn on **Wireless debugging** and
   allow it on this network. Tap it to open it.
2. Tap **Pair device with pairing code**. Keep this dialog on screen,
   e.g. in split screen with droid-nix: pairing fails once Settings is in
   the background.
3. In droid-nix, with the port and code from the dialog:

   ```sh
   adb pair localhost:PORT CODE
   ```

Then, and whenever Wireless debugging was turned on again:

```sh
droid-nix-adb-connect
adb shell pm list packages
adb shell settings get global adb_wifi_enabled
```

Wireless debugging listens on a new port every time.
`droid-nix-adb-connect` finds it over mDNS (`adb mdns` does not work:
the adb in nixpkgs is built without mDNS) and runs `adb connect
localhost:PORT`.

Android turns Wireless debugging off when Wi-Fi disconnects and at every
boot; it does not work without Wi-Fi. The pairing stays: turn Wireless
debugging on again (the "Wireless debugging" tile under Quick Settings
developer tiles makes this one tap) and run `droid-nix-adb-connect`.
Every program in droid-nix can use the pairing key `~/.android/adbkey`;
**Forget** droid-nix@localhost under Paired devices revokes it.
